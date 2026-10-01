#pragma once

#include "core/Types.h"
#include "gpu/IGpuContext.h"
#include <memory>
#include <vector>
#include <deque>
#include <mutex>

namespace Lingjing {

    // ============================================================================
    // 场景类型
    // ============================================================================

    enum class SceneType : uint32_t {
        Unknown = 0,
        Static,         // 静止画面
        SlowMotion,     // 缓慢移动
        NormalMotion,   // 正常运动
        FastMotion,     // 快速运动
        SceneCut,       // 场景切换
        Flash,          // 闪光（瞬时）
        UI,             // UI 界面
    };

    inline const char* sceneTypeName(SceneType t) {
        switch (t) {
        case SceneType::Static:       return "Static";
        case SceneType::SlowMotion:   return "Slow Motion";
        case SceneType::NormalMotion: return "Normal Motion";
        case SceneType::FastMotion:   return "Fast Motion";
        case SceneType::SceneCut:     return "Scene Cut";
        case SceneType::Flash:        return "Flash";
        case SceneType::UI:           return "UI";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // 场景分析结果
    // ============================================================================

    struct SceneAnalysis {
        SceneType type = SceneType::Unknown;

        // 全局运动幅度（像素）
        float globalMotionMagnitude = 0.0f;

        // 运动一致性（0~1，1 表示全局一致）
        float motionConsistency = 0.0f;

        // 画面亮度
        float meanLuma = 0.0f;
        float lumaVariance = 0.0f;

        // 直方图差异（用于场景切换检测）
        float histogramDiff = 0.0f;

        // 场景切换标志
        bool isSceneCut = false;
        bool isFlash = false;

        // 置信度
        float confidence = 0.0f;

        // 时间戳
        TimestampNs timestampNs = 0;
        FrameIndex frameIndex = 0;
    };

    // ============================================================================
    // 场景检测器配置
    // ============================================================================

    struct SceneDetectorConfig {
        // 直方图
        int histogramBins = 64;

        // 场景切换阈值
        float sceneCutThreshold = 0.45f;

        // 闪光检测
        float flashLumaThreshold = 0.3f;
        uint32_t flashMaxDurationFrames = 5;

        // 运动幅度阈值（像素/帧）
        float staticThreshold = 0.5f;
        float slowMotionThreshold = 3.0f;
        float fastMotionThreshold = 20.0f;

        // UI 检测
        bool enableUIDetection = true;
        float uiEdgeDensityThreshold = 0.1f;

        // 时域平滑
        int temporalWindowSize = 10;
    };

    // ============================================================================
    // 场景检测器
    // ============================================================================

    class SceneDetector {
    public:
        SceneDetector();
        ~SceneDetector();

        bool initialize(IGpuContext* gpuContext,
            const SceneDetectorConfig& config);

        void shutdown();

        // 分析一帧
        bool analyzeFrame(const GpuTexture& currentFrame,
            const GpuTexture& prevFrame,
            SceneAnalysis& result);

        // 仅基于光流分析
        void analyzeFromFlow(const GpuTexture& flowField,
            SceneAnalysis& result);

        // 重置（切换游戏时）
        void reset();

        // 查询
        const SceneAnalysis& lastAnalysis() const { return lastAnalysis_; }
        SceneType currentSceneType() const { return currentType_; }

        bool isReady() const { return initialized_; }

    private:
        IGpuContext* gpuContext_ = nullptr;
        SceneDetectorConfig config_;
        bool initialized_ = false;

        SceneAnalysis lastAnalysis_;
        SceneType currentType_ = SceneType::Unknown;

        // 直方图历史
        std::deque<std::vector<float>> histogramHistory_;
        std::mutex mutex_;

        // 闪光跟踪
        uint32_t flashCounter_ = 0;

        // 内部方法
        void computeHistogram(const GpuTexture& frame,
            std::vector<float>& histogram);

        float compareHistograms(const std::vector<float>& h1,
            const std::vector<float>& h2);

        void classifyScene(SceneAnalysis& analysis);
    };

} // namespace Lingjing