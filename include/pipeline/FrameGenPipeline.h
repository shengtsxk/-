#pragma once

#include "core/Types.h"
#include "core/DeviceCaps.h"
#include "capture/ICapture.h"
#include "gpu/IGpuContext.h"
#include "flow/IFlowEngine.h"
#include "ai/IAIRepair.h"
#include "pipeline/LatencyController.h"
#include "pipeline/SceneDetector.h"
#include "pipeline/PerformanceMonitor.h"
#include "pipeline/OcclusionHandler.h"
#include "pipeline/FrameBlender.h"
#include "pipeline/MultiFrameGenerator.h"
#include "pipeline/Presenter.h"

#include <memory>
#include <atomic>
#include <thread>
#include <functional>

namespace Lingjing {

    // ============================================================================
    // 管线配置
    // ============================================================================

    struct PipelineConfig {
        // 捕获
        CaptureConfig captureConfig;

        // 帧生成
        FrameGenSettings frameGenSettings;

        // 光流
        FlowSolveConfig flowConfig;

        // AI 修复
        AIRepairConfig aiConfig;

        // 遮挡
        OcclusionConfig occlusionConfig;

        // 混合
        BlendConfig blendConfig;

        // 多帧
        MultiFrameConfig multiFrameConfig;

        // 呈现
        PresentConfig presentConfig;

        // 延迟
        LatencyTarget latencyTarget = LatencyTarget::Quality;

        // 场景检测
        SceneDetectorConfig sceneConfig;

        // 性能监控
        bool enablePerformanceMonitor = true;
        uint32_t perfSampleWindow = 300;
    };

    // ============================================================================
    // 管线状态
    // ============================================================================

    enum class PipelineState : uint32_t {
        Uninitialized = 0,
        Initializing,
        Ready,
        Running,
        Paused,
        Stopping,
        Error,
    };

    inline const char* pipelineStateName(PipelineState s) {
        switch (s) {
        case PipelineState::Uninitialized: return "Uninitialized";
        case PipelineState::Initializing:  return "Initializing";
        case PipelineState::Ready:         return "Ready";
        case PipelineState::Running:       return "Running";
        case PipelineState::Paused:        return "Paused";
        case PipelineState::Stopping:      return "Stopping";
        case PipelineState::Error:         return "Error";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // 管线统计
    // ============================================================================

    struct PipelineStats {
        PipelineState state = PipelineState::Uninitialized;

        // 帧数
        uint64_t capturedFrames = 0;
        uint64_t generatedFrames = 0;
        uint64_t presentedFrames = 0;
        uint64_t droppedFrames = 0;

        // 性能
        float currentSourceFps = 0.0f;
        float currentOutputFps = 0.0f;
        float avgTotalMs = 0.0f;
        float avgCaptureMs = 0.0f;
        float avgFlowMs = 0.0f;
        float avgOcclusionMs = 0.0f;
        float avgBlendMs = 0.0f;
        float avgAiMs = 0.0f;
        float avgPresentMs = 0.0f;

        // 当前状态
        SceneType currentScene = SceneType::Unknown;
        DynamicQuality currentQuality = DynamicQuality::High;
        float convergence = 0.0f;

        // GPU
        float gpuUtilization = 0.0f;
        float gpuTempC = 0.0f;
        float vramUsedMB = 0.0f;
    };

    // ============================================================================
    // 主管线
    // ============================================================================

    class FrameGenPipeline {
    public:
        FrameGenPipeline();
        ~FrameGenPipeline();

        // ====================================================================
        // 生命周期
        // ====================================================================

        bool initialize(const PipelineConfig& config);

        bool start();
        void pause();
        void resume();
        void stop();
        void shutdown();

        // ====================================================================
        // 查询
        // ====================================================================

        PipelineState state() const { return state_.load(); }
        bool isRunning() const {
            return state_.load() == PipelineState::Running;
        }

        PipelineStats stats() const;

        // 各组件访问
        ICapture* capture() const { return capture_.get(); }
        IGpuContext* gpuContext() const { return gpuContext_.get(); }
        IFlowEngine* flowEngine() const { return flowEngine_.get(); }
        IAIRepair* aiRepair() const { return aiRepair_.get(); }

        LatencyController& latencyController() { return *latencyController_; }
        SceneDetector& sceneDetector() { return *sceneDetector_; }
        PerformanceMonitor& performanceMonitor() { return *performanceMonitor_; }

        // ====================================================================
        // 动态配置
        // ====================================================================

        void setMultiplier(uint32_t multiplier);
        void setQualityLevel(QualityLevel level);
        void setLatencyTarget(LatencyTarget target);
        void setSceneDetectionEnabled(bool enabled);

        // 帧生成总开关（false = 直通，不插帧）
        void setFrameGenEnabled(bool enabled);

        // 超分辨率（开关 + 倍率，运行中动态切换）
        void setSuperResolution(bool enabled, uint32_t scale);

        // ====================================================================
        // 回调
        // ====================================================================

        using StateChangeCallback =
            std::function<void(PipelineState oldState, PipelineState newState)>;

        using ErrorCallback = std::function<void(const Error& error)>;

        void setStateChangeCallback(StateChangeCallback cb) {
            stateCallback_ = std::move(cb);
        }

        void setErrorCallback(ErrorCallback cb) {
            errorCallback_ = std::move(cb);
        }

    private:
        // 主循环
        void pipelineThreadProc();

        // 处理单帧
        bool processOneFrame();

        // 状态转换
        void setState(PipelineState newState);

        // 错误处理
        void handleError(const Error& error);

        // 组件
        std::unique_ptr<ICapture> capture_;
        std::unique_ptr<IGpuContext> gpuContext_;
        std::unique_ptr<IFlowEngine> flowEngine_;
        std::unique_ptr<IAIRepair> aiRepair_;
        std::unique_ptr<LatencyController> latencyController_;
        std::unique_ptr<SceneDetector> sceneDetector_;
        std::unique_ptr<PerformanceMonitor> performanceMonitor_;
        std::unique_ptr<OcclusionHandler> occlusionHandler_;
        std::unique_ptr<FrameBlender> frameBlender_;
        std::unique_ptr<MultiFrameGenerator> multiFrameGenerator_;
        std::unique_ptr<Presenter> presenter_;

        // 配置
        PipelineConfig config_;

        // 状态
        std::atomic<PipelineState> state_{ PipelineState::Uninitialized };
        std::atomic<bool> stopRequested_{ false };
        std::atomic<bool> pauseRequested_{ false };

        std::thread pipelineThread_;
        mutable std::mutex mutex_;

        // 统计
        PipelineStats stats_;

        // 前一帧
        GpuTexture prevFrame_;
        bool hasPrevFrame_ = false;

        // GPU 检测
        GpuInfo selectedGpu_;

        // 回调
        StateChangeCallback stateCallback_;
        ErrorCallback errorCallback_;
    };

} // namespace Lingjing