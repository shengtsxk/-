#include "pipeline/OcclusionHandler.h"
#include "core/Logger.h"
#include "core/Timer.h"

#ifdef LJ_NVIDIA
#include <cuda_runtime.h>

// CUDA 内核声明
extern "C" {
    bool ljLaunchConsistency(
        const float2* fwdFlow,
        const float2* bwdFlow,
        float* errorMap,
        int W, int H,
        cudaStream_t stream);

    bool ljLaunchOcclusionProbability(
        const float* errorMap,
        float* occProb,
        int W, int H,
        float tau, float alpha,
        cudaStream_t stream);

    bool ljLaunchBoundaryHardening(
        const float* occProb,
        float* occSharp,
        int W, int H,
        float gradThreshold,
        cudaStream_t stream);

    bool ljLaunchOcclusionInfill(
        const float2* flowIn,
        const float* occProb,
        float2* flowOut,
        int W, int H,
        int radius,
        float occThresh,
        cudaStream_t stream);

    bool ljLaunchBilateralInfill(
        const float2* flowIn,
        const float* guide,
        const float* occProb,
        float2* flowOut,
        int W, int H,
        int radius,
        float occThresh,
        float sigmaR,
        cudaStream_t stream);
}
#endif

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    OcclusionHandler::OcclusionHandler() = default;
    OcclusionHandler::~OcclusionHandler() { shutdown(); }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool OcclusionHandler::initialize(IGpuContext* gpuContext,
        const OcclusionConfig& config)
    {
        if (initialized_) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("OcclusionHandler: invalid GPU context");
            return false;
        }

        gpuContext_ = gpuContext;
        config_ = config;
        initialized_ = true;

        LOG_INFO("OcclusionHandler initialized");
        return true;
    }

    void OcclusionHandler::shutdown() {
        if (!initialized_) return;

        cachedErrorMap.reset();
        cachedOccProb.reset();
        cachedOccMask.reset();
        cachedInfillFlow.reset();

        cachedWidth_ = 0;
        cachedHeight_ = 0;

        gpuContext_ = nullptr;
        initialized_ = false;
    }

    // ============================================================================
    // 缓冲管理
    // ============================================================================

    bool OcclusionHandler::ensureBuffers(uint32_t W, uint32_t H) {
        if (cachedWidth_ == W && cachedHeight_ == H &&
            cachedErrorMap.valid())
        {
            return true;
        }

        // 重新分配
        auto errBuf = gpuContext_->createTexture(W, H, TextureFormat::R32_FLOAT);
        auto probBuf = gpuContext_->createTexture(W, H, TextureFormat::R32_FLOAT);
        auto maskBuf = gpuContext_->createTexture(W, H, TextureFormat::R8_UNORM);
        auto infillBuf = gpuContext_->createTexture(W, H, TextureFormat::R32G32_FLOAT);

        if (!errBuf || !probBuf || !maskBuf || !infillBuf) {
            LOG_ERROR("OcclusionHandler: buffer allocation failed");
            return false;
        }

        cachedErrorMap.nativeHandle = errBuf->nativeHandle();
        cachedErrorMap.width = W;
        cachedErrorMap.height = H;
        cachedErrorMap.format = TextureFormat::R32_FLOAT;

        cachedOccProb.nativeHandle = probBuf->nativeHandle();
        cachedOccProb.width = W;
        cachedOccProb.height = H;
        cachedOccProb.format = TextureFormat::R32_FLOAT;

        cachedOccMask.nativeHandle = maskBuf->nativeHandle();
        cachedOccMask.width = W;
        cachedOccMask.height = H;
        cachedOccMask.format = TextureFormat::R8_UNORM;

        cachedInfillFlow.nativeHandle = infillBuf->nativeHandle();
        cachedInfillFlow.width = W;
        cachedInfillFlow.height = H;
        cachedInfillFlow.format = TextureFormat::R32G32_FLOAT;

        cachedWidth_ = W;
        cachedHeight_ = H;

        return true;
    }

    // ============================================================================
    // 计算遮挡
    // ============================================================================

    bool OcclusionHandler::computeOcclusion(const GpuTexture& fwdFlow,
        const GpuTexture& bwdFlow,
        OcclusionResult& result)
    {
        if (!initialized_) return false;

        if (!fwdFlow.valid() || !bwdFlow.valid()) {
            LOG_ERROR("OcclusionHandler: invalid flow inputs");
            return false;
        }

        uint32_t W = fwdFlow.width;
        uint32_t H = fwdFlow.height;

        if (!ensureBuffers(W, H)) return false;

#ifdef LJ_NVIDIA
        cudaStream_t stream = static_cast<cudaStream_t>(
            gpuContext_->defaultStream().nativeHandle());

        // 1. 前向-后向一致性
        bool ok = ljLaunchConsistency(
            static_cast<const float2*>(fwdFlow.nativeHandle),
            static_cast<const float2*>(bwdFlow.nativeHandle),
            static_cast<float*>(cachedErrorMap.nativeHandle),
            static_cast<int>(W), static_cast<int>(H),
            stream);

        if (!ok) {
            LOG_ERROR("OcclusionHandler: consistency kernel failed");
            return false;
        }

        // 2. 遮挡概率
        ok = ljLaunchOcclusionProbability(
            static_cast<const float*>(cachedErrorMap.nativeHandle),
            static_cast<float*>(cachedOccProb.nativeHandle),
            static_cast<int>(W), static_cast<int>(H),
            config_.consistencyThreshold,
            3.0f,
            stream);

        if (!ok) {
            LOG_ERROR("OcclusionHandler: probability kernel failed");
            return false;
        }

        // 3. 边界硬化
        if (config_.enableBoundaryHardening) {
            ok = ljLaunchBoundaryHardening(
                static_cast<const float*>(cachedOccProb.nativeHandle),
                static_cast<float*>(cachedOccProb.nativeHandle),
                static_cast<int>(W), static_cast<int>(H),
                config_.boundaryGradientThreshold,
                stream);

            if (!ok) {
                LOG_WARN("OcclusionHandler: boundary hardening failed");
            }
        }

        result.occlusionProb = cachedOccProb;
        result.occlusionMask = cachedOccMask;

        // 估算遮挡比例（简化：假设 10%）
        result.occlusionRatio = 0.10f;
        result.averageConfidence = 0.85f;

        return true;
#else
        LOG_ERROR("OcclusionHandler: no backend available");
        return false;
#endif
    }

    // ============================================================================
    // 光流修补
    // ============================================================================

    bool OcclusionHandler::infillFlow(const GpuTexture& flowIn,
        const GpuTexture& occProb,
        const GpuTexture& guideLuma,
        GpuTexture& flowOut)
    {
        if (!initialized_) return false;
        if (!flowIn.valid() || !occProb.valid()) return false;

        uint32_t W = flowIn.width;
        uint32_t H = flowIn.height;

        if (!ensureBuffers(W, H)) return false;

#ifdef LJ_NVIDIA
        cudaStream_t stream = static_cast<cudaStream_t>(
            gpuContext_->defaultStream().nativeHandle());

        bool ok = false;

        if (guideLuma.valid()) {
            ok = ljLaunchBilateralInfill(
                static_cast<const float2*>(flowIn.nativeHandle),
                static_cast<const float*>(guideLuma.nativeHandle),
                static_cast<const float*>(occProb.nativeHandle),
                static_cast<float2*>(cachedInfillFlow.nativeHandle),
                static_cast<int>(W), static_cast<int>(H),
                config_.infillRadius,
                config_.infillOccThreshold,
                0.1f,
                stream);
        }
        else {
            ok = ljLaunchOcclusionInfill(
                static_cast<const float2*>(flowIn.nativeHandle),
                static_cast<const float*>(occProb.nativeHandle),
                static_cast<float2*>(cachedInfillFlow.nativeHandle),
                static_cast<int>(W), static_cast<int>(H),
                config_.infillRadius,
                config_.infillOccThreshold,
                stream);
        }

        if (!ok) {
            LOG_ERROR("OcclusionHandler: infill kernel failed");
            return false;
        }

        flowOut = cachedInfillFlow;
        return true;
#else
        return false;
#endif
    }

} // namespace Lingjing