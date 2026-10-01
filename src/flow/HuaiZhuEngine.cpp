#include "flow/HuaiZhuEngine.h"
#include "core/Logger.h"
#include "core/Timer.h"

#include <chrono>
#include <cmath>

#ifdef LJ_NVIDIA
#include <cuda_runtime.h>
#endif

namespace Lingjing {

    // ============================================================================
    // 内部实现
    // ============================================================================

    struct HuaiZhuEngine::Impl {
        IGpuContext* gpuContext = nullptr;
        FlowSolveConfig config;
        bool initialized = false;
        bool warmedUp = false;

        // 淮竹核心组件
        std::unique_ptr<TimeGeometryMemory> tgm;
        std::unique_ptr<MultiCueProjectionBeam> mcpb;
        std::unique_ptr<Coherent3DFlowEngine> coherent3d;
        std::unique_ptr<AdaptiveConfidenceField> acf;

        // 帧缓冲
        GpuTexture cachedDepth;
        GpuTexture cachedGray;
        GpuTexture cachedPrevGray;
        uint32_t cachedWidth = 0;
        uint32_t cachedHeight = 0;

        // 前一帧光流（时域平滑）
        GpuTexture cachedPrevFlow;

        // 计数器
        uint64_t frameCounter = 0;

        // 时间戳
        TimestampNs lastFrameNs = 0;

        // 学习上下文
        void* learningContext = nullptr;
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    HuaiZhuEngine::HuaiZhuEngine()
        : impl_(std::make_unique<Impl>()) {
    }

    HuaiZhuEngine::~HuaiZhuEngine() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool HuaiZhuEngine::initialize(IGpuContext* gpuContext,
        const FlowSolveConfig& config)
    {
        if (impl_->initialized) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("HuaiZhu: invalid GPU context");
            return false;
        }

        impl_->gpuContext = gpuContext;
        impl_->config = config;

        // 创建时间几何记忆
        impl_->tgm = std::make_unique<TimeGeometryMemory>();

        // 创建多线索投影束
        impl_->mcpb = std::make_unique<MultiCueProjectionBeam>();
        impl_->mcpb->setFovPrior(config.huaiZhu.defaultFov);

        // 创建 3D 相干流场
        impl_->coherent3d = std::make_unique<Coherent3DFlowEngine>();
        if (!impl_->coherent3d->initialize(gpuContext)) {
            LOG_ERROR("HuaiZhu: Coherent3DFlow init failed");
            return false;
        }

        // 创建自适应可信度场
        impl_->acf = std::make_unique<AdaptiveConfidenceField>();
        if (!impl_->acf->initialize(gpuContext)) {
            LOG_ERROR("HuaiZhu: AdaptiveConfidenceField init failed");
            return false;
        }

        impl_->initialized = true;
        LOG_INFO("HuaiZhu engine initialized: %ux%u, precision=%d",
            config.inputWidth, config.inputHeight,
            static_cast<int>(config.precision));

        return true;
    }

    void HuaiZhuEngine::shutdown() {
        if (!impl_->initialized) return;

        if (impl_->coherent3d) impl_->coherent3d->shutdown();
        if (impl_->acf) impl_->acf->shutdown();

        impl_->coherent3d.reset();
        impl_->acf.reset();
        impl_->tgm.reset();
        impl_->mcpb.reset();

        impl_->initialized = false;
        impl_->warmedUp = false;

        LOG_INFO("HuaiZhu engine shutdown");
    }

    // ============================================================================
    // 主求解
    // ============================================================================

    bool HuaiZhuEngine::solve(const GpuTexture& prevFrame,
        const GpuTexture& currFrame,
        FlowResult& result)
    {
        if (!impl_->initialized) {
            LOG_ERROR("HuaiZhu: not initialized");
            return false;
        }

        if (!prevFrame.valid() || !currFrame.valid()) {
            LOG_ERROR("HuaiZhu: invalid frame inputs");
            return false;
        }

        Timer totalTimer;
        totalTimer.start();

        result.reset();

        uint32_t W = currFrame.width;
        uint32_t H = currFrame.height;

        // 尺寸变化：重建缓冲
        if (impl_->cachedWidth != W || impl_->cachedHeight != H) {
            impl_->cachedWidth = W;
            impl_->cachedHeight = H;

            auto depthBuf = impl_->gpuContext->createTexture(
                W, H, TextureFormat::R32_FLOAT);
            auto grayBuf = impl_->gpuContext->createTexture(
                W, H, TextureFormat::R32_FLOAT);
            auto prevGrayBuf = impl_->gpuContext->createTexture(
                W, H, TextureFormat::R32_FLOAT);

            if (depthBuf) {
                impl_->cachedDepth.nativeHandle = depthBuf->nativeHandle();
                impl_->cachedDepth.width = W;
                impl_->cachedDepth.height = H;
                impl_->cachedDepth.format = TextureFormat::R32_FLOAT;
            }

            if (grayBuf) {
                impl_->cachedGray.nativeHandle = grayBuf->nativeHandle();
                impl_->cachedGray.width = W;
                impl_->cachedGray.height = H;
                impl_->cachedGray.format = TextureFormat::R32_FLOAT;
            }

            if (prevGrayBuf) {
                impl_->cachedPrevGray.nativeHandle = prevGrayBuf->nativeHandle();
                impl_->cachedPrevGray.width = W;
                impl_->cachedPrevGray.height = H;
                impl_->cachedPrevGray.format = TextureFormat::R32_FLOAT;
            }

            impl_->tgm->reset();
            impl_->warmedUp = false;
        }

        // 计算帧间隔
        float dt = 0.016f;
        if (impl_->lastFrameNs > 0 && currFrame.timestampNs > 0) {
            dt = static_cast<float>(currFrame.timestampNs - impl_->lastFrameNs)
                * 1e-9f;
            if (dt < 0.001f || dt > 0.5f) dt = 0.016f;
        }
        impl_->lastFrameNs = currFrame.timestampNs;

        // ------------------------------------------------------------------
        // 1. 更新 TGM（预测）
        // ------------------------------------------------------------------
        auto predictedState = impl_->tgm->predict(dt);

        Timer geometryTimer;
        geometryTimer.start();

        // ------------------------------------------------------------------
        // 2. 提取几何观测（消失点等）
        // ------------------------------------------------------------------
        GeometryObservations obs;
        obs.frameWidth = static_cast<int>(W);
        obs.frameHeight = static_cast<int>(H);

        // 注：消失点检测需要在 GPU 上执行
        // 这里简化为调用外部几何分析模块
        // 完整实现使用 Hough 变换 + 消失点提取

        // ------------------------------------------------------------------
        // 3. 更新 TGM
        // ------------------------------------------------------------------
        impl_->tgm->update(obs);

        // ------------------------------------------------------------------
        // 4. 融合投影参数
        // ------------------------------------------------------------------
        const auto& state = impl_->tgm->currentState();

        float estimatedFocal = state.fx > 0.0f ? state.fx : (W * 0.8f);
        float estimatedCx = state.cx > 0.0f ? state.cx : (W * 0.5f);
        float estimatedCy = state.cy > 0.0f ? state.cy : (H * 0.5f);

        float geometryTimeMs = geometryTimer.elapsedMs();
        result.geometryTimeMs = geometryTimeMs;

        // ------------------------------------------------------------------
        // 5. 深度估计（每 N 帧一次）
        // ------------------------------------------------------------------
        Timer depthTimer;
        depthTimer.start();

        // 注：深度估计由 AI 模块完成，这里使用缓存
        const GpuTexture& depthTex = impl_->cachedDepth;

        float depthTimeMs = depthTimer.elapsedMs();
        result.depthTimeMs = depthTimeMs;

        // ------------------------------------------------------------------
        // 6. 3D 相干流场
        // ------------------------------------------------------------------
        Coherent3DFlowResult geoResult;

        if (depthTex.valid()) {
            CameraIntrinsics intrinsics;
            intrinsics.fx = estimatedFocal;
            intrinsics.fy = estimatedFocal;
            intrinsics.cx = estimatedCx;
            intrinsics.cy = estimatedCy;
            intrinsics.znear = 0.1f;
            intrinsics.zfar = 1000.0f;

            if (!impl_->coherent3d->compute(depthTex, state.pose,
                intrinsics, geoResult)) {
                LOG_WARN("HuaiZhu: 3D flow computation failed");
            }
        }

        // ------------------------------------------------------------------
        // 7. 2D 光流（回退 / 补充）
        // ------------------------------------------------------------------
        // 注：由上层调用外部光流引擎或硬件光流
        // 这里假设已经通过其他路径获取了 flowTexture

        // ------------------------------------------------------------------
        // 8. 融合（贝叶斯）
        // ------------------------------------------------------------------
        Timer consistencyTimer;
        consistencyTimer.start();

        if (geoResult.isValid() && depthTex.valid()) {
            AdaptiveConfidenceResult fused;

            // 简化：如果没有 2D 光流，直接使用几何流
            // 完整实现需要 2D 光流输入
            result.forwardFlow = geoResult.flow;
            result.hasForwardFlow = true;
            result.occlusionProb = geoResult.consistency;
            result.hasOcclusionProb = true;
            result.confidence = geoResult.consistency;
            result.hasConfidence = true;
        }

        float consistencyTimeMs = consistencyTimer.elapsedMs();
        result.consistencyTimeMs = consistencyTimeMs;

        // ------------------------------------------------------------------
        // 9. 时域平滑
        // ------------------------------------------------------------------
        // 注：在 AdaptiveConfidenceField::temporalSmooth 中实现

        // ------------------------------------------------------------------
        // 10. 更新收敛状态
        // ------------------------------------------------------------------
        impl_->warmedUp = state.isConverged();

        result.totalTimeMs = totalTimer.elapsedMs();
        result.estimatedQuality = state.projectionConvergence;
        result.epeEstimate = state.isConverged() ? 0.05f : 0.5f;

        impl_->frameCounter++;

        return true;
    }

    // ============================================================================
    // 重置
    // ============================================================================

    void HuaiZhuEngine::reset() {
        if (impl_->tgm) impl_->tgm->reset();
        if (impl_->mcpb) impl_->mcpb->clear();

        impl_->warmedUp = false;
        impl_->frameCounter = 0;
        impl_->lastFrameNs = 0;

        LOG_INFO("HuaiZhu engine reset");
    }

    // ============================================================================
    // 查询
    // ============================================================================

    bool HuaiZhuEngine::isReady() const {
        return impl_->initialized;
    }

    FlowEngineType HuaiZhuEngine::type() const {
        return FlowEngineType::HuaiZhu;
    }

    const char* HuaiZhuEngine::engineName() const {
        return "淮竹 HuaiZhu";
    }

    GpuVendor HuaiZhuEngine::vendor() const {
        if (!impl_->gpuContext) return GpuVendor::Unknown;
        return impl_->gpuContext->vendor();
    }

    float HuaiZhuEngine::convergence() const {
        if (!impl_->tgm) return 0.0f;
        return impl_->tgm->currentState().projectionConvergence;
    }

    bool HuaiZhuEngine::isWarmedUp() const {
        return impl_->warmedUp;
    }

    void HuaiZhuEngine::setLearningContext(void* context) {
        impl_->learningContext = context;
    }

    void* HuaiZhuEngine::learningContext() const {
        return impl_->learningContext;
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IFlowEngine> createHuaiZhuEngine() {
        return std::make_unique<HuaiZhuEngine>();
    }

} // namespace Lingjing