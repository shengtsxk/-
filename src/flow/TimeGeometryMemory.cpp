#include "flow/TimeGeometryMemory.h"
#include "core/Logger.h"
#include "core/MathUtils.h"

#include <cmath>
#include <algorithm>

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    TimeGeometryMemory::TimeGeometryMemory() {
        state_.reset();
    }

    TimeGeometryMemory::~TimeGeometryMemory() = default;

    // ============================================================================
    // 预测
    // ============================================================================

    GeometryState TimeGeometryMemory::predict(float dt) {
        std::lock_guard<std::mutex> lock(mutex_);

        GeometryState predicted = state_;

        // 匀速运动模型：位姿 += 速度 * dt
        for (size_t i = 0; i < 6; ++i) {
            predicted.pose[i] += state_.velocity[i] * dt;
        }

        // 速度不确定度增长（布朗运动）
        for (size_t i = 0; i < 6; ++i) {
            float v = state_.velSigma[i];
            predicted.velSigma[i] = std::sqrt(v * v + 0.5f * dt * dt);
        }

        // 位姿不确定度增长
        for (size_t i = 0; i < 6; ++i) {
            float p = state_.poseSigma[i];
            float v = state_.velSigma[i];
            predicted.poseSigma[i] = std::sqrt(p * p + v * v * dt * dt);
        }

        // 投影参数变化缓慢
        for (size_t i = 0; i < 4; ++i) {
            predicted.projSigma[i] = state_.projSigma[i] * 1.001f;
        }

        predicted.frameIndex = state_.frameIndex + 1;
        predicted.timestampNs = state_.timestampNs +
            static_cast<TimestampNs>(dt * 1e9f);

        return predicted;
    }

    // ============================================================================
    // 标量卡尔曼更新
    // ============================================================================

    void TimeGeometryMemory::kalmanScalarUpdate(
        float& state, float& sigma,
        float measurement, float measSigma)
    {
        if (measSigma < 1e-8f) measSigma = 1e-8f;

        float S = sigma * sigma + measSigma * measSigma;
        if (S < 1e-12f) return;

        float K = sigma * sigma / S;

        state = state + K * (measurement - state);
        sigma = sigma * std::sqrt(1.0f - K);

        // 防下限
        if (sigma < 1e-6f) sigma = 1e-6f;
    }

    // ============================================================================
    // 更新
    // ============================================================================

    void TimeGeometryMemory::update(const GeometryObservations& obs) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 直接调用更新逻辑（不加锁的版本）
        if (!obs.vpXs.empty()) {
            updateProjection(obs);
        }

        if (!obs.matchedX1.empty()) {
            updatePose(obs);
        }

        if (obs.hasJitter && obs.jitterConfidence > 0.5f) {
            // TAA 抖动提供高精度投影观测
            float measSigma = 0.1f * (1.0f - obs.jitterConfidence);
            kalmanScalarUpdate(state_.fx, state_.projSigma[0],
                state_.fx, measSigma);
        }

        updateVelocity();
        updateConvergence();

        // 加入历史
        history_.push_back(state_);
        if (history_.size() > kMaxHistory) {
            history_.pop_front();
        }

        framesProcessed_++;
    }

    // ============================================================================
    // 投影参数更新
    // ============================================================================

    void TimeGeometryMemory::updateProjection(const GeometryObservations& obs) {
        float cx = state_.cx;
        float cy = state_.cy;

        // 初始主点：图像中心
        if (cx == 0.0f && obs.frameWidth > 0) {
            cx = static_cast<float>(obs.frameWidth) * 0.5f;
            cy = static_cast<float>(obs.frameHeight) * 0.5f;
            state_.cx = cx;
            state_.cy = cy;
        }

        // 从消失点对恢复焦距
        std::vector<float> focalCandidates;
        focalCandidates.reserve(obs.vpXs.size() * obs.vpXs.size() / 2);

        int n = static_cast<int>(obs.vpXs.size());

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                float d1x = obs.vpXs[i] - cx;
                float d1y = obs.vpYs[i] - cy;
                float d2x = obs.vpXs[j] - cx;
                float d2y = obs.vpYs[j] - cy;

                // 两个消失点对应的方向正交时，点积应为 -f²
                float dot = d1x * d2x + d1y * d2y;

                if (dot < -1e-3f) {
                    float f = std::sqrt(-dot);
                    if (f > 200.0f && f < 20000.0f) {
                        // 权重由两个消失点的置信度决定
                        float w = obs.vpWeights[i] * obs.vpWeights[j];
                        // 用整数权重插入
                        int repeat = std::max(1, static_cast<int>(w * 4.0f));
                        for (int r = 0; r < repeat; ++r) {
                            focalCandidates.push_back(f);
                        }
                    }
                }
            }
        }

        if (focalCandidates.size() < 3) return;

        // 鲁棒中值
        std::sort(focalCandidates.begin(), focalCandidates.end());
        float medianF = focalCandidates[focalCandidates.size() / 2];

        // MAD（中值绝对偏差）
        std::vector<float> deviations;
        deviations.reserve(focalCandidates.size());
        for (float f : focalCandidates) {
            deviations.push_back(std::fabs(f - medianF));
        }
        std::sort(deviations.begin(), deviations.end());
        float mad = deviations[deviations.size() / 2];

        // 测量不确定度
        float measSigma = 1.4826f * mad;

        // 卡尔曼更新
        kalmanScalarUpdate(state_.fx, state_.projSigma[0],
            medianF, measSigma);
        kalmanScalarUpdate(state_.fy, state_.projSigma[1],
            medianF, measSigma);

        state_.projectionValid = true;
    }

    // ============================================================================
    // 位姿更新
    // ============================================================================

    void TimeGeometryMemory::updatePose(const GeometryObservations& obs) {
        int n = static_cast<int>(obs.matchedX1.size());
        if (n < 8) return;

        // 简化：用平均位移估计平移
        // 完整实现应使用 PnP 或对极几何

        float sumDx = 0.0f, sumDy = 0.0f, totalW = 0.0f;

        for (int i = 0; i < n; ++i) {
            float dx = obs.matchedX2[i] - obs.matchedX1[i];
            float dy = obs.matchedY2[i] - obs.matchedY1[i];
            float w = obs.matchedWeights.empty()
                ? 1.0f : obs.matchedWeights[i];

            sumDx += w * dx;
            sumDy += w * dy;
            totalW += w;
        }

        if (totalW < 1e-3f) return;

        float avgDx = sumDx / totalW;
        float avgDy = sumDy / totalW;

        // 转换为归一化坐标下的平移
        float f = state_.fx;
        if (f < 100.0f) f = 1000.0f;  // 防异常

        float tx = avgDx / f;
        float ty = avgDy / f;

        // 卡尔曼更新
        kalmanScalarUpdate(state_.pose[3], state_.poseSigma[3], tx, 0.01f);
        kalmanScalarUpdate(state_.pose[4], state_.poseSigma[4], ty, 0.01f);

        state_.poseValid = true;
    }

    // ============================================================================
    // 速度更新
    // ============================================================================

    void TimeGeometryMemory::updateVelocity() {
        if (history_.empty()) return;

        const auto& prev = history_.back();

        float dt = static_cast<float>(state_.timestampNs -
            prev.timestampNs) * 1e-9f;

        if (dt < 1e-3f || dt > 1.0f) return;

        for (size_t i = 0; i < 6; ++i) {
            float delta = state_.pose[i] - prev.pose[i];
            float velMeas = delta / dt;

            kalmanScalarUpdate(state_.velocity[i],
                state_.velSigma[i],
                velMeas,
                0.5f);
        }
    }

    // ============================================================================
    // 收敛度更新
    // ============================================================================

    void TimeGeometryMemory::updateConvergence() {
        // 投影收敛度
        float projUncert = 0.0f;
        for (size_t i = 0; i < 4; ++i) {
            projUncert += state_.projSigma[i];
        }
        state_.projectionConvergence = std::exp(-projUncert / 10.0f);

        // 位姿收敛度
        float poseUncert = 0.0f;
        for (size_t i = 0; i < 6; ++i) {
            poseUncert += state_.poseSigma[i];
        }
        state_.poseConvergence = std::exp(-poseUncert / 1.0f);

        // 稳定帧计数
        if (state_.projectionConvergence > 0.9f &&
            state_.poseConvergence > 0.85f) {
            state_.stableFrames++;
        }
        else {
            state_.stableFrames = 0;
        }
    }

    // ============================================================================
    // 重置
    // ============================================================================

    void TimeGeometryMemory::reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.reset();
        history_.clear();
        framesProcessed_ = 0;
    }

    // ============================================================================
    // 学习数据初始化
    // ============================================================================

    void TimeGeometryMemory::seedFromLearning(
        float fx, float fy, float cx, float cy, float confidence)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        state_.fx = fx;
        state_.fy = fy;
        state_.cx = cx;
        state_.cy = cy;

        // 学习数据的置信度转不确定度
        float sigma = 0.05f * (1.0f - confidence) + 0.01f;
        state_.projSigma[0] = sigma;
        state_.projSigma[1] = sigma;
        state_.projSigma[2] = sigma;
        state_.projSigma[3] = sigma;

        state_.projectionValid = true;
        state_.projectionConvergence = confidence;
        state_.stableFrames = 100;  // 假装已稳定

        LOG_INFO("TGM seeded from learning: fx=%.1f, conf=%.2f",
            fx, confidence);
    }

} // namespace Lingjing