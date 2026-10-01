#include "flow/MultiCueProjection.h"
#include "core/Logger.h"
#include "core/MathUtils.h"

#include <cmath>
#include <algorithm>
#include <numeric>

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    MultiCueProjectionBeam::MultiCueProjectionBeam() = default;
    MultiCueProjectionBeam::~MultiCueProjectionBeam() = default;

    // ============================================================================
    // 线索管理
    // ============================================================================

    void MultiCueProjectionBeam::addCue(const ProjectionCue& cue) {
        if (!cue.isValid()) return;

        // 丢弃过期观测
        if (latestTimestamp_ > 0 &&
            cue.timestampNs < latestTimestamp_ - 100000000) {
            return;
        }

        // 同类型替换
        for (auto& c : cues_) {
            if (c.type == cue.type) {
                c = cue;
                latestTimestamp_ = (std::max)(latestTimestamp_, cue.timestampNs);
                return;
            }
        }

        cues_.push_back(cue);
        latestTimestamp_ = (std::max)(latestTimestamp_, cue.timestampNs);
    }

    void MultiCueProjectionBeam::prune(TimestampNs currentTimestampNs) {
        const TimestampNs maxAge = 500000000;  // 500ms

        cues_.erase(
            std::remove_if(cues_.begin(), cues_.end(),
                [currentTimestampNs, maxAge](const ProjectionCue& c) {
                    return (currentTimestampNs - c.timestampNs) > maxAge;
                }),
            cues_.end());
    }

    void MultiCueProjectionBeam::clear() {
        cues_.clear();
        latestTimestamp_ = 0;
    }

    void MultiCueProjectionBeam::setFovPrior(float fovDegrees) {
        if (fovDegrees > 30.0f && fovDegrees < 150.0f) {
            fovPriorDegrees_ = fovDegrees;
        }
    }

    // ============================================================================
    // 融合
    // ============================================================================

    ProjectionFusionResult MultiCueProjectionBeam::fuse(
        float frameWidth, float frameHeight) const
    {
        ProjectionFusionResult result;

        std::vector<ProjectionCue> validCues;
        validCues.reserve(cues_.size() + 1);

        for (const auto& c : cues_) {
            if (c.isValid()) validCues.push_back(c);
        }

        // 加入 FOV 先验（始终存在）
        ProjectionCue prior;
        prior.type = ProjectionCueType::FOVPrior;
        prior.value = focalFromFov(fovPriorDegrees_, frameWidth);
        prior.sigma = prior.value * 0.25f;
        prior.confidence = 0.4f;
        prior.timestampNs = 0;
        validCues.push_back(prior);

        if (validCues.empty()) return result;

        return irlsFuse(validCues);
    }

    // ============================================================================
    // IRLS 鲁棒融合
    // ============================================================================

    ProjectionFusionResult MultiCueProjectionBeam::irlsFuse(
        const std::vector<ProjectionCue>& validCues) const
    {
        ProjectionFusionResult result;

        if (validCues.empty()) return result;

        // 初值：加权平均
        float sumW = 0.0f;
        float sumWV = 0.0f;

        for (const auto& c : validCues) {
            float w = c.confidence / (c.sigma * c.sigma + 1e-6f);
            sumW += w;
            sumWV += w * c.value;
        }

        float estimate = (sumW > 1e-6f) ? (sumWV / sumW) : validCues[0].value;

        // 迭代重加权（IRLS）
        const float huberDelta = 3.0f;
        const int maxIterations = 5;

        for (int iter = 0; iter < maxIterations; ++iter) {
            float iterSumW = 0.0f;
            float iterSumWV = 0.0f;

            for (const auto& c : validCues) {
                float residual = c.value - estimate;
                float absR = std::fabs(residual);

                // Huber 权重
                float robustW = 1.0f;
                if (absR > huberDelta) {
                    robustW = huberDelta / absR;
                }

                float w = c.confidence * robustW / (c.sigma * c.sigma + 1e-6f);
                iterSumW += w;
                iterSumWV += w * c.value;
            }

            if (iterSumW > 1e-6f) {
                float newEstimate = iterSumWV / iterSumW;
                // 收敛检查
                if (std::fabs(newEstimate - estimate) < 1e-4f) {
                    estimate = newEstimate;
                    break;
                }
                estimate = newEstimate;
            }
        }

        // 计算最终 sigma
        float varianceSum = 0.0f;
        float totalW = 0.0f;

        for (const auto& c : validCues) {
            float w = c.confidence / (c.sigma * c.sigma + 1e-6f);
            float d = c.value - estimate;
            varianceSum += w * d * d;
            totalW += w;
        }

        float variance = (totalW > 1e-6f) ? (varianceSum / totalW) : 0.0f;

        // 计算最大残差
        float maxResidual = 0.0f;

        for (const auto& c : validCues) {
            float r = std::fabs(c.value - estimate);
            if (r > maxResidual) maxResidual = r;

            int idx = static_cast<int>(c.type);
            if (idx >= 0 && idx < 4) {
                result.residuals[idx] = c.value - estimate;
            }
        }

        result.focal = estimate;
        result.sigma = std::sqrt(variance);
        result.numCues = static_cast<int>(validCues.size());
        result.confidence = std::exp(
            -maxResidual / (estimate * 0.1f + 1e-6f));

        return result;
    }

    // ============================================================================
    // 焦距/FOV 转换
    // ============================================================================

    float MultiCueProjectionBeam::focalFromFov(float fovDegrees,
        float imageWidth)
    {
        float fovRad = fovDegrees * Math::kDegToRad;
        float halfFov = fovRad * 0.5f;

        float tanHalf = std::tan(halfFov);
        if (tanHalf < 1e-6f) tanHalf = 1e-6f;

        return (imageWidth * 0.5f) / tanHalf;
    }

    float MultiCueProjectionBeam::fovFromFocal(float focal,
        float imageWidth)
    {
        if (focal < 1e-6f) return 90.0f;

        float tanHalf = (imageWidth * 0.5f) / focal;
        float halfFov = std::atan(tanHalf);

        return halfFov * 2.0f * Math::kRadToDeg;
    }

    // ============================================================================
    // 消失点线索
    // ============================================================================

    ProjectionCue VanishingPointCueExtractor::extract(
        const std::vector<float>& vpXs,
        const std::vector<float>& vpYs,
        const std::vector<float>& weights,
        float cx, float cy,
        TimestampNs timestampNs)
    {
        ProjectionCue cue;
        cue.type = ProjectionCueType::VanishingPoint;
        cue.timestampNs = timestampNs;
        cue.confidence = 0.0f;

        if (vpXs.size() < 2) return cue;

        std::vector<float> focalCandidates;
        std::vector<float> candidateWeights;

        int n = static_cast<int>(vpXs.size());

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                float d1x = vpXs[i] - cx;
                float d1y = vpYs[i] - cy;
                float d2x = vpXs[j] - cx;
                float d2y = vpYs[j] - cy;

                float dot = d1x * d2x + d1y * d2y;

                if (dot < -1e-3f) {
                    float f = std::sqrt(-dot);
                    if (f > 200.0f && f < 20000.0f) {
                        focalCandidates.push_back(f);
                        candidateWeights.push_back(weights[i] * weights[j]);
                    }
                }
            }
        }

        if (focalCandidates.size() < 3) return cue;

        // 加权中值
        std::vector<size_t> indices(focalCandidates.size());
        std::iota(indices.begin(), indices.end(), 0);

        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) {
                return focalCandidates[a] < focalCandidates[b];
            });

        float totalWeight = 0.0f;
        for (float w : candidateWeights) totalWeight += w;

        float halfWeight = totalWeight * 0.5f;
        float cumWeight = 0.0f;
        float weightedMedian = focalCandidates[indices[0]];

        for (size_t idx : indices) {
            cumWeight += candidateWeights[idx];
            if (cumWeight >= halfWeight) {
                weightedMedian = focalCandidates[idx];
                break;
            }
        }

        // MAD
        std::vector<float> deviations;
        deviations.reserve(focalCandidates.size());
        for (float f : focalCandidates) {
            deviations.push_back(std::fabs(f - weightedMedian));
        }
        std::sort(deviations.begin(), deviations.end());
        float mad = deviations[deviations.size() / 2];

        cue.value = weightedMedian;
        cue.sigma = mad * 1.4826f;
        cue.confidence = (std::min)(
            static_cast<float>(focalCandidates.size()) / 10.0f, 1.0f);

        return cue;
    }

    // ============================================================================
    // TAA 抖动线索
    // ============================================================================

    ProjectionCue TAAJitterCueExtractor::extract(
        float jitterMagnitude,
        float expectedJitter,
        float correlation,
        TimestampNs timestampNs)
    {
        ProjectionCue cue;
        cue.type = ProjectionCueType::TAAJitter;
        cue.timestampNs = timestampNs;

        if (jitterMagnitude < 1e-6f) {
            cue.confidence = 0.0f;
            return cue;
        }

        // 抖动幅度与焦距成反比
        float ratio = expectedJitter / jitterMagnitude;

        cue.value = ratio;
        cue.sigma = 0.05f * (1.0f - correlation);
        cue.confidence = correlation;

        return cue;
    }

    // ============================================================================
    // FOV 先验线索
    // ============================================================================

    ProjectionCue FOVPriorCueExtractor::extract(
        float fovDegrees,
        float frameWidth,
        TimestampNs timestampNs)
    {
        ProjectionCue cue;
        cue.type = ProjectionCueType::FOVPrior;

        cue.value = MultiCueProjectionBeam::focalFromFov(fovDegrees, frameWidth);
        cue.sigma = cue.value * 0.25f;
        cue.confidence = 0.4f;
        cue.timestampNs = timestampNs;

        return cue;
    }

    // ============================================================================
    // 自运动线索
    // ============================================================================

    ProjectionCue SelfMotionCueExtractor::extract(
        const std::vector<float>& flowX,
        const std::vector<float>& flowY,
        const std::vector<float>& depths,
        const std::vector<float>& xCoords,
        const std::vector<float>& yCoords,
        float cx, float cy,
        TimestampNs timestampNs)
    {
        ProjectionCue cue;
        cue.type = ProjectionCueType::SelfMotionConsistency;
        cue.timestampNs = timestampNs;
        cue.confidence = 0.0f;

        size_t n = flowX.size();
        if (n < 20 || depths.size() != n || xCoords.size() != n) return cue;

        // 对每个点：
        // u ≈ -f * tx / Z + f * X * tz / Z²
        // 其中 X = (x - cx) / f * Z
        // 所以 u * Z / (x - cx) ≈ -f * tx / (x - cx) + tz * Z / f

        // 简化：使用 (u * Z, (x-cx)) 的线性关系估计焦距
        // u * Z ≈ -f * tx + (x-cx) * tz * Z / f

        float sumA = 0.0f, sumB = 0.0f, sumAB = 0.0f, sumAA = 0.0f;

        for (size_t i = 0; i < n; ++i) {
            float Z = depths[i];
            if (Z < 0.1f || Z > 1000.0f) continue;

            float xNorm = xCoords[i] - cx;
            float u = flowX[i];

            float A = xNorm * Z;
            float B = u * Z;

            sumA += A;
            sumB += B;
            sumAB += A * B;
            sumAA += A * A;
        }

        if (sumAA < 1e-6f) return cue;

        // 线性拟合斜率 k = sum(A*B - meanA*meanB) / sum(A² - meanA²)
        float invN = 1.0f / static_cast<float>(n);
        float meanA = sumA * invN;
        float meanB = sumB * invN;

        float num = sumAB - sumA * meanB;
        float den = sumAA - sumA * meanA;

        if (std::fabs(den) < 1e-6f) return cue;

        float slope = num / den;

        // slope ≈ tz / f（当 tx 较小时）
        // 实际焦距需要知道 tz，这里只给相对估计
        // 使用典型游戏 tz ≈ 1 pixel/frame 的假设
        // 更精确的实现需要多帧

        if (slope > 1e-6f) {
            float estimatedF = 1.0f / slope;  // 相对估计

            // 与图像尺寸匹配的绝对估计
            float typicalF = static_cast<float>(cx) * 1.3f;
            estimatedF = typicalF * (1.0f + (estimatedF - 1.0f) * 0.1f);

            cue.value = estimatedF;
            cue.sigma = estimatedF * 0.15f;
            cue.confidence = 0.6f;
        }

        return cue;
    }

} // namespace Lingjing