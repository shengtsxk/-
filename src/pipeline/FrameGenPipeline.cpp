#include "pipeline/FrameGenPipeline.h"
#include "capture/CaptureSelector.h"
#include "gpu/GpuSelector.h"
#include "flow/FlowEngineFactory.h"
#include "ai/AIRepairFactory.h"
#include "core/Logger.h"
#include "core/Timer.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

#ifdef LJ_NVIDIA
#include <nvml.h>
#endif

#endif

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    FrameGenPipeline::FrameGenPipeline() = default;

    FrameGenPipeline::~FrameGenPipeline() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool FrameGenPipeline::initialize(const PipelineConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_.load() != PipelineState::Uninitialized) {
            LOG_WARN("FrameGenPipeline already initialized");
            return true;
        }

        setState(PipelineState::Initializing);

        config_ = config;

        // ---- 1. GPU 检测 ----
        {
            auto gpus = detectAllGpus();
            if (gpus.empty()) {
                handleError(Error(ErrorCode::GpuNotFound, "No GPUs detected"));
                return false;
            }

            auto selection = GpuSelector::selectDefault(gpus);
            if (selection.selectedIndex < 0) {
                handleError(Error(ErrorCode::GpuNotFound,
                    "No suitable GPU found"));
                return false;
            }

            selectedGpu_ = selection.selectedGpu;

            LOG_INFO("Pipeline GPU: %s", selectedGpu_.toDisplayString().c_str());
        }

        // ---- 2. GPU 上下文 ----
        switch (selectedGpu_.vendor) {
        case GpuVendor::NVIDIA:
            gpuContext_ = createCudaContext();
            break;
        case GpuVendor::Intel:
            gpuContext_ = createSyclContext();
            break;
        default:
            handleError(Error(ErrorCode::GpuNotFound,
                "Unsupported GPU vendor"));
            return false;
        }

        if (!gpuContext_ || !gpuContext_->initialize(selectedGpu_)) {
            LOG_WARN("GPU context initialization failed, continuing in CPU mode");
            gpuContext_.reset();  // 核心功能不依赖 GPU 上下文
        }

        // ---- 3. 捕获 ----
        if (config_.captureConfig.targetHwnd == nullptr) {
            handleError(Error(ErrorCode::WindowNotFound,
                "No target window specified"));
            return false;
        }

        capture_ = createWGCCapture();  // 默认 WGC

        if (!capture_ || !capture_->initialize(config_.captureConfig)) {
            handleError(Error(ErrorCode::CaptureInitFailed,
                "Failed to initialize capture"));
            return false;
        }

        // ---- 4. 光流引擎 ----
        {
            FlowEngineType type = FlowEngineFactory::selectBestType(selectedGpu_);

            flowEngine_ = FlowEngineFactory::create(type);

            if (!flowEngine_) {
                handleError(Error(ErrorCode::FlowInitFailed,
                    "Failed to create flow engine"));
                return false;
            }

            // 配置输入尺寸
            config_.flowConfig.inputWidth = capture_->frameSize().width;
            config_.flowConfig.inputHeight = capture_->frameSize().height;
            config_.flowConfig.outputWidth = config_.flowConfig.inputWidth;
            config_.flowConfig.outputHeight = config_.flowConfig.inputHeight;

            if (!flowEngine_->initialize(gpuContext_.get(), config_.flowConfig)) {
                LOG_WARN("Flow engine initialization failed, continuing without");
                flowEngine_.reset();  // 插帧核心由 Presenter CPU 路径承担
            } else {
                LOG_INFO("Flow engine: %s", flowEngine_->engineName());
            }
        }

        // ---- 5. AI 修复 ----
        if (config_.frameGenSettings.aiRepairEnabled) {
            config_.aiConfig = AIRepairFactory::autoConfigure(selectedGpu_);

            if (config_.aiConfig.backend == AIRepairBackend::None) {
                LOG_WARN("AI repair unavailable: no usable backend for this GPU "
                    "(TensorRT needs CUDA, OpenVINO needs oneAPI; DirectML not built). "
                    "Continuing without AI repair.");
            }
            else if (config_.aiConfig.modelPath.empty()) {
                LOG_WARN("AI repair: backend=%d but no model configured, continuing without",
                    static_cast<int>(config_.aiConfig.backend));
            }
            else {
                aiRepair_ = AIRepairFactory::create(config_.aiConfig.backend);

                if (aiRepair_ && !aiRepair_->initialize(gpuContext_.get(),
                    config_.aiConfig)) {
                    LOG_WARN("AI repair initialization failed, continuing without");
                    aiRepair_.reset();
                }
                else if (aiRepair_) {
                    LOG_INFO("AI repair: %s", aiRepair_->engineName());
                }
            }
        }

        // ---- 6. 遮挡处理器 ----
        occlusionHandler_ = std::make_unique<OcclusionHandler>();

        if (!occlusionHandler_->initialize(gpuContext_.get(),
            config_.occlusionConfig)) {
            LOG_WARN("Occlusion handler initialization failed, continuing without");
            occlusionHandler_.reset();
        }
        frameBlender_ = std::make_unique<FrameBlender>();

        if (!frameBlender_->initialize(gpuContext_.get(), config_.blendConfig)) {
            LOG_WARN("Frame blender initialization failed, continuing without");
            frameBlender_.reset();
        }

        // ---- 8. 多帧生成器 ----
        multiFrameGenerator_ = std::make_unique<MultiFrameGenerator>();
        if (!multiFrameGenerator_->initialize(
            gpuContext_.get(),
            flowEngine_.get(),
            occlusionHandler_.get(),
            frameBlender_.get(),
            aiRepair_.get()))
        {
            handleError(Error(ErrorCode::PipelineNotInitialized,
                "Failed to initialize multi-frame generator"));
            return false;
        }

        // ---- 9. 场景检测器 ----
        sceneDetector_ = std::make_unique<SceneDetector>();

        if (!sceneDetector_->initialize(gpuContext_.get(), config_.sceneConfig)) {
            LOG_WARN("Scene detector initialization failed");
        }

        // ---- 10. 延迟控制器 ----
        latencyController_ = std::make_unique<LatencyController>();
        latencyController_->setTarget(config_.latencyTarget);

        // ---- 11. 性能监控器 ----
        if (config_.enablePerformanceMonitor) {
            performanceMonitor_ = std::make_unique<PerformanceMonitor>();
            performanceMonitor_->setSampleWindow(config_.perfSampleWindow);
        }

        // ---- 12. 呈现器 ----
        presenter_ = std::make_unique<Presenter>();

        if (!presenter_->initialize(config_.captureConfig.targetHwnd,
            config_.presentConfig)) {
            LOG_WARN("Presenter initialization failed");
        }
        presenter_->setMultiplier(config_.frameGenSettings.multiplier);

        setState(PipelineState::Ready);

        LOG_INFO("FrameGenPipeline initialized successfully");
        return true;
    }

    // ============================================================================
    // 启动/停止
    // ============================================================================

    bool FrameGenPipeline::start() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_.load() == PipelineState::Running) {
            return true;
        }

        if (state_.load() != PipelineState::Ready &&
            state_.load() != PipelineState::Paused)
        {
            LOG_ERROR("Pipeline not ready to start");
            return false;
        }

        // 启动捕获
        if (capture_ && !capture_->isRunning()) {
            if (!capture_->start()) {
                handleError(Error(ErrorCode::CaptureInitFailed,
                    "Failed to start capture"));
                return false;
            }
        }

        // 启动性能监控
        if (performanceMonitor_) {
            performanceMonitor_->start();
        }

        stopRequested_ = false;
        pauseRequested_ = false;

        // 启动主循环线程
        if (!pipelineThread_.joinable()) {
            pipelineThread_ = std::thread(
                [this] { pipelineThreadProc(); });
        }

        setState(PipelineState::Running);

        LOG_INFO("FrameGenPipeline started");
        return true;
    }

    void FrameGenPipeline::pause() {
        if (state_.load() != PipelineState::Running) return;

        pauseRequested_ = true;

        LOG_INFO("FrameGenPipeline paused");
    }

    void FrameGenPipeline::resume() {
        if (state_.load() != PipelineState::Paused) return;

        pauseRequested_ = false;
        setState(PipelineState::Running);

        LOG_INFO("FrameGenPipeline resumed");
    }

    void FrameGenPipeline::stop() {
        if (state_.load() == PipelineState::Stopping ||
            state_.load() == PipelineState::Uninitialized) {
            return;
        }

        stopRequested_ = true;

        setState(PipelineState::Stopping);

        // 停止捕获
        if (capture_ && capture_->isRunning()) {
            capture_->stop();
        }

        // 等待线程
        if (pipelineThread_.joinable()) {
            pipelineThread_.join();
        }

        // 停止监控
        if (performanceMonitor_) {
            performanceMonitor_->stop();
        }

        setState(PipelineState::Ready);

        LOG_INFO("FrameGenPipeline stopped");
    }

    void FrameGenPipeline::shutdown() {
        stop();

        std::lock_guard<std::mutex> lock(mutex_);

        if (multiFrameGenerator_) {
            multiFrameGenerator_->shutdown();
            multiFrameGenerator_.reset();
        }

        if (presenter_) {
            presenter_->shutdown();
            presenter_.reset();
        }

        if (frameBlender_) {
            frameBlender_->shutdown();
            frameBlender_.reset();
        }

        if (occlusionHandler_) {
            occlusionHandler_->shutdown();
            occlusionHandler_.reset();
        }

        if (performanceMonitor_) {
            performanceMonitor_.reset();
        }

        if (latencyController_) {
            latencyController_.reset();
        }

        if (sceneDetector_) {
            sceneDetector_->shutdown();
            sceneDetector_.reset();
        }

        if (aiRepair_) {
            aiRepair_->shutdown();
            aiRepair_.reset();
        }

        if (flowEngine_) {
            flowEngine_->shutdown();
            flowEngine_.reset();
        }

        if (capture_) {
            capture_->shutdown();
            capture_.reset();
        }

        if (gpuContext_) {
            gpuContext_->shutdown();
            gpuContext_.reset();
        }

        setState(PipelineState::Uninitialized);

        LOG_INFO("FrameGenPipeline shutdown complete");
    }

    // ============================================================================
    // 主循环
    // ============================================================================

    void FrameGenPipeline::pipelineThreadProc() {
        LOG_INFO("Pipeline thread started");

        // 设置线程优先级为高
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

        while (!stopRequested_.load()) {
            // 暂停处理
            if (pauseRequested_.load()) {
                if (state_.load() != PipelineState::Paused) {
                    setState(PipelineState::Paused);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (state_.load() == PipelineState::Paused) {
                setState(PipelineState::Running);
            }

            // 处理一帧
            try {
                if (!processOneFrame()) {
                    static thread_local int idleCount = 0;
                    ++idleCount;
                    if (idleCount <= 5 || idleCount % 100 == 0) {
                        LOG_INFO("Pipeline idle: grabFrame fail #%d", idleCount);
                    }
                    // 无新捕获帧（目标窗口静止）：WGC 对静止画面不产生新帧属正常语义。
                    // 周期性调用 presentIfReady 触发 Presenter 的 BitBlt 回退路径，
                    // 让输出窗口持续显示目标窗口画面（而非停帧/黑屏），
                    // 同时执行 followTargetWindow，使超分开启时输出窗口按倍率放大。
                    if (idleCount % 3 == 0 && presenter_) {
                        presenter_->presentIfReady();
                    }
                    // 错误后短延迟（提高至 10ms，降低静止画面下的忙转 CPU）
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("Pipeline frame exception: %s", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            catch (...) {
                LOG_ERROR("Pipeline frame unknown exception");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        LOG_INFO("Pipeline thread stopped");
    }

    // ============================================================================
    // 处理一帧
    // ============================================================================

    bool FrameGenPipeline::processOneFrame() {
        Timer totalTimer;
        totalTimer.start();

        // ---- 1. 捕获帧 ----
        Timer captureTimer;
        captureTimer.start();

        GpuTexture currentFrame;

        if (!capture_->grabFrame(currentFrame, 16)) {
            return false;  // 超时
        }

        float captureMs = static_cast<float>(captureTimer.elapsedMs());

        stats_.capturedFrames++;

        static thread_local int procCount = 0;
        if (++procCount <= 5 || procCount % 60 == 0) {
            LOG_INFO("Pipeline processed frame #%llu (proc#%d)",
                static_cast<unsigned long long>(stats_.capturedFrames),
                procCount);
        }

        // ---- 2. 场景检测 ----
        SceneAnalysis sceneAnalysis;

        if (sceneDetector_ && hasPrevFrame_) {
            try {
                sceneDetector_->analyzeFrame(currentFrame, prevFrame_, sceneAnalysis);
            }
            catch (...) {
                LOG_WARN("Scene analysis exception ignored");
            }
        }

        // ---- 3. 生成 N 帧 ----
        Timer frameGenTimer;
        frameGenTimer.start();

        MultiFrameResult genResult;

        if (hasPrevFrame_ && config_.frameGenSettings.enabled) {
            MultiFrameConfig mfConfig = config_.multiFrameConfig;
            mfConfig.multiplier = config_.frameGenSettings.multiplier;
            mfConfig.enableAIRepair = config_.frameGenSettings.aiRepairEnabled;

            if (!multiFrameGenerator_->generateFrames(
                prevFrame_,
                currentFrame,
                prevFrame_,
                mfConfig,
                genResult))
            {
                LOG_WARN("Frame generation failed");
            }
        }

        float frameGenMs = static_cast<float>(frameGenTimer.elapsedMs());

        // ---- 4. 呈现生成帧 ----
        Timer presentTimer;
        presentTimer.start();

        if (genResult.numFrames > 0 && presenter_) {
            TimestampNs now = nowNs();
            TimestampNs frameInterval = currentFrame.timestampNs -
                prevFrame_.timestampNs;

            for (uint32_t i = 0; i < genResult.numFrames; ++i) {
                TimestampNs targetTs = prevFrame_.timestampNs +
                    static_cast<TimestampNs>(
                        static_cast<double>(frameInterval) *
                        (static_cast<double>(i + 1) /
                            static_cast<double>(genResult.numFrames + 1)));

                presenter_->submitFrame(genResult.frames[i], targetTs);

                stats_.generatedFrames++;
            }
        }

        // 立即呈现当前帧
        presenter_->submitFrame(currentFrame, nowNs());

        // 触发呈现
        presenter_->presentIfReady();  // 每帧循环呈现一次（真实输出窗口显示）

        float presentMs = static_cast<float>(presentTimer.elapsedMs());

        // ---- 5. 更新延迟控制器 ----
        float totalMs = static_cast<float>(totalTimer.elapsedMs());

        if (latencyController_) {
            latencyController_->recordStages(
                captureMs,
                genResult.flowMs,
                0.0f,
                genResult.aiMs,
                genResult.blendMs,
                presentMs,
                totalMs);
        }

        // ---- 6. 更新性能监控 ----
        if (performanceMonitor_) {
            PerformanceSample sample;
            sample.timestampNs = currentFrame.timestampNs;
            sample.captureMs = captureMs;
            sample.flowMs = genResult.flowMs;
            sample.occlusionMs = genResult.occlusionMs;
            sample.blendMs = genResult.blendMs;
            sample.aiRepairMs = genResult.aiMs;
            sample.presentMs = presentMs;
            sample.totalMs = totalMs;

            performanceMonitor_->recordSample(sample);
        }

        // ---- 7a. 周期性性能摘要（每 30 帧） ----
        {
            static thread_local int perfLogCounter = 0;
            if (++perfLogCounter % 30 == 0 && performanceMonitor_) {
                auto perf = performanceMonitor_->summary();
                LOG_INFO("[Perf] captured=%llu generated=%llu source=%.1fFPS output=%.1fFPS "
                    "capture=%.2fms flow=%.2fms blend=%.2fms present=%.2fms total=%.2fms "
                    "quality=%.3f",
                    static_cast<unsigned long long>(stats_.capturedFrames),
                    static_cast<unsigned long long>(stats_.generatedFrames),
                    perf.avgSourceFps, perf.avgOutputFps,
                    perf.avgCaptureMs, perf.avgFlowMs, perf.avgBlendMs,
                    perf.avgPresentMs, perf.avgTotalMs,
                    genResult.avgQuality);
            }
        }

        // ---- 7. 保存当前帧为上一帧 ----
        prevFrame_ = currentFrame;
        hasPrevFrame_ = true;

        return true;
    }

    // ============================================================================
    // 状态转换
    // ============================================================================

    void FrameGenPipeline::setState(PipelineState newState) {
        PipelineState oldState = state_.exchange(newState);

        if (oldState == newState) return;

        LOG_INFO("Pipeline state: %s -> %s",
            pipelineStateName(oldState),
            pipelineStateName(newState));

        if (stateCallback_) {
            stateCallback_(oldState, newState);
        }
    }

    // ============================================================================
    // 错误处理
    // ============================================================================

    void FrameGenPipeline::handleError(const Error& error) {
        LOG_ERROR("Pipeline error: [%s] %s",
            errorCodeName(error.code),
            error.message.c_str());

        if (errorCallback_) {
            errorCallback_(error);
        }

        setState(PipelineState::Error);
    }

    // ============================================================================
    // 查询
    // ============================================================================

    PipelineStats FrameGenPipeline::stats() const {
        std::lock_guard<std::mutex> lock(mutex_);

        PipelineStats s = stats_;
        s.state = state_.load();

        if (performanceMonitor_) {
            auto perf = performanceMonitor_->summary();
            s.avgTotalMs = perf.avgTotalMs;
            s.avgCaptureMs = perf.avgCaptureMs;
            s.avgFlowMs = perf.avgFlowMs;
            s.avgBlendMs = perf.avgBlendMs;
            s.avgAiMs = perf.avgAiRepairMs;
            s.avgPresentMs = perf.avgPresentMs;
            s.currentSourceFps = perf.avgSourceFps;
            s.currentOutputFps = perf.avgOutputFps;
        }

        if (sceneDetector_) {
            s.currentScene = sceneDetector_->currentSceneType();
        }

        if (latencyController_) {
            s.currentQuality = latencyController_->currentQuality();
        }

        if (flowEngine_) {
            s.convergence = flowEngine_->convergence();
        }

        return s;
    }

    // ============================================================================
    // 动态配置
    // ============================================================================

    void FrameGenPipeline::setFrameGenEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.frameGenSettings.enabled = enabled;
        LOG_INFO("Frame generation %s", enabled ? "enabled" : "disabled (passthrough)");
    }

    void FrameGenPipeline::setSuperResolution(bool enabled, uint32_t scale) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.presentConfig.superResEnabled = enabled;
        config_.presentConfig.superResScale = scale;
        if (presenter_) {
            presenter_->setSuperResolution(enabled, scale);
        }
    }

    void FrameGenPipeline::setMultiplier(uint32_t multiplier) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (multiplier < 1) multiplier = 1;
        if (multiplier > 20) multiplier = 20;

        config_.multiFrameConfig.multiplier = multiplier;
        config_.frameGenSettings.multiplier = multiplier;

        LOG_INFO("Multiplier set to %ux", multiplier);
    }

    void FrameGenPipeline::setQualityLevel(QualityLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);

        config_.frameGenSettings.quality = level;

        // 根据质量档位调整参数
        switch (level) {
        case QualityLevel::Performance:
            config_.flowConfig.precision = FlowSolveConfig::Precision::Fast;
            config_.aiConfig.enableFp16 = true;
            break;

        case QualityLevel::Balanced:
            config_.flowConfig.precision = FlowSolveConfig::Precision::Balanced;
            break;

        case QualityLevel::Quality:
            config_.flowConfig.precision = FlowSolveConfig::Precision::Quality;
            break;

        case QualityLevel::Ultra:
            config_.flowConfig.precision = FlowSolveConfig::Precision::Ultra;
            config_.aiConfig.enableFp16 = false;  // 使用 FP32
            break;
        }

        LOG_INFO("Quality level set to %s", qualityLevelName(level));
    }

    void FrameGenPipeline::setLatencyTarget(LatencyTarget target) {
        std::lock_guard<std::mutex> lock(mutex_);

        config_.latencyTarget = target;

        if (latencyController_) {
            latencyController_->setTarget(target);
        }
    }

    void FrameGenPipeline::setSceneDetectionEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);

        config_.sceneConfig.enableUIDetection = enabled;
    }

} // namespace Lingjing
