#pragma once

#include "core/Types.h"

#include <array>
#include <deque>
#include <mutex>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 几何状态（位姿 + 投影参数 + 不确定度）
    // ============================================================================

    struct GeometryState {
        // 位姿 [tx, ty, tz, rx, ry, rz]
        std::array<float, 6> pose{};

        // 速度
        std::array<float, 6> velocity{};

        // 速度不确定度（sigma）
        std::array<float, 6> velSigma{};

        // 位姿不确定度
        std::array<float, 6> poseSigma{};

        // 投影不确定度 [fx, fy, cx, cy]
        std::array<float, 4> projSigma{};

        // 投影参数
        float fx = 0.0f;
        float fy = 0.0f;
        float cx = 0.0f;
        float cy = 0.0f;

        bool projectionValid = false;
        bool poseValid = false;

        float projectionConvergence = 0.0f;
        float poseConvergence = 0.0f;

        uint32_t stableFrames = 0;
        uint64_t frameIndex = 0;
        TimestampNs timestampNs = 0;

        bool isConverged() const { return projectionConvergence > 0.9f && poseConvergence > 0.85f; }

        void reset() {
            pose.fill(0.0f);
            velocity.fill(0.0f);
            velSigma.fill(1.0f);
            poseSigma.fill(1.0f);
            projSigma.fill(1.0f);
            fx = fy = cx = cy = 0.0f;
            projectionValid = false;
            poseValid = false;
            projectionConvergence = 0.0f;
            poseConvergence = 0.0f;
            stableFrames = 0;
            frameIndex = 0;
            timestampNs = 0;
        }
    };

    // ============================================================================
    // 几何观测（从帧中提取）
    // ============================================================================

    struct GeometryObservations {
        // 消失点
        std::vector<float> vpXs;
        std::vector<float> vpYs;
        std::vector<float> vpWeights;

        // 匹配点
        std::vector<float> matchedX1;
        std::vector<float> matchedY1;
        std::vector<float> matchedX2;
        std::vector<float> matchedY2;
        std::vector<float> matchedWeights;

        int frameWidth = 0;
        int frameHeight = 0;

        // TAA 抖动
        bool hasJitter = false;
        float jitterConfidence = 0.0f;
    };

    // ============================================================================
    // 时间几何记忆
    // ============================================================================

    class TimeGeometryMemory {
    public:
        TimeGeometryMemory();
        ~TimeGeometryMemory();

        // 预测下一时刻状态
        GeometryState predict(float dt);

        // 用观测更新状态
        void update(const GeometryObservations& obs);

        // 重置
        void reset();

        // 从学习数据初始化投影
        void seedFromLearning(
            float fx, float fy, float cx, float cy, float confidence);

        // 获取当前状态（线程安全拷贝）
        GeometryState currentState() {
            std::lock_guard<std::mutex> lock(mutex_);
            return state_;
        }

    private:
        static constexpr size_t kMaxHistory = 120;

        void updateProjection(const GeometryObservations& obs);
        void updatePose(const GeometryObservations& obs);
        void updateVelocity();
        void updateConvergence();

        static void kalmanScalarUpdate(
            float& state, float& sigma,
            float measurement, float measSigma);

        std::mutex mutex_;
        GeometryState state_;
        std::deque<GeometryState> history_;
        uint64_t framesProcessed_ = 0;
    };

} // namespace Lingjing
