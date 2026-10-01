#pragma once

#include "core/Types.h"
#include <vector>
#include <array>

namespace Lingjing {

    // ============================================================================
    // 投影线索类型
    // ============================================================================

    enum class ProjectionCueType : uint32_t {
        VanishingPoint = 0,
        TAAJitter = 1,
        FOVPrior = 2,
        SelfMotionConsistency = 3,
        Count = 4,
    };

    struct ProjectionCue {
        ProjectionCueType type = ProjectionCueType::FOVPrior;
        float value = 0.0f;         // 焦距估计
        float sigma = 0.0f;          // 标准差
        float confidence = 0.0f;     // 置信度 0~1
        TimestampNs timestampNs = 0;

        bool isValid() const {
            return sigma > 0.0f && confidence > 0.1f;
        }
    };

    // ============================================================================
    // 融合结果
    // ============================================================================

    struct ProjectionFusionResult {
        float focal = 0.0f;
        float sigma = 0.0f;
        float confidence = 0.0f;
        int numCues = 0;

        std::array<float, 4> residuals = { 0 };

        bool isValid() const {
            return focal > 0.0f && confidence > 0.1f;
        }
    };

    // ============================================================================
    // 多线索投影束
    // ============================================================================

    class MultiCueProjectionBeam {
    public:
        MultiCueProjectionBeam();
        ~MultiCueProjectionBeam();

        // 添加线索观测
        void addCue(const ProjectionCue& cue);

        // 融合
        ProjectionFusionResult fuse(float frameWidth, float frameHeight) const;

        // 设置 FOV 先验
        void setFovPrior(float fovDegrees);

        // 清理过期线索
        void prune(TimestampNs currentTimestampNs);

        // 清空
        void clear();

        // 静态工具
        static float focalFromFov(float fovDegrees, float imageWidth);
        static float fovFromFocal(float focal, float imageWidth);

    private:
        std::vector<ProjectionCue> cues_;
        TimestampNs latestTimestamp_ = 0;
        float fovPriorDegrees_ = 78.0f;

        // 鲁棒迭代重加权
        ProjectionFusionResult irlsFuse(
            const std::vector<ProjectionCue>& validCues) const;
    };

    // ============================================================================
    // 线索提取器
    // ============================================================================

    class VanishingPointCueExtractor {
    public:
        // 从消失点集合估计焦距
        static ProjectionCue extract(
            const std::vector<float>& vpXs,
            const std::vector<float>& vpYs,
            const std::vector<float>& weights,
            float cx, float cy,
            TimestampNs timestampNs);
    };

    class TAAJitterCueExtractor {
    public:
        static ProjectionCue extract(
            float jitterMagnitude,
            float expectedJitter,
            float correlation,
            TimestampNs timestampNs);
    };

    class FOVPriorCueExtractor {
    public:
        static ProjectionCue extract(
            float fovDegrees,
            float frameWidth,
            TimestampNs timestampNs);
    };

    class SelfMotionCueExtractor {
    public:
        static ProjectionCue extract(
            const std::vector<float>& flowX,
            const std::vector<float>& flowY,
            const std::vector<float>& depths,
            const std::vector<float>& xCoords,
            const std::vector<float>& yCoords,
            float cx, float cy,
            TimestampNs timestampNs);
    };

} // namespace Lingjing