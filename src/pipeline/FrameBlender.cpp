#include "pipeline/FrameBlender.h"
#include "core/Logger.h"
#include "core/Timer.h"

#ifdef LJ_NVIDIA
#include <cuda_runtime.h>

extern "C" {
    bool ljLaunchOcclusionAwareBlend(
        const float* warpFrom1,
        const float* warpFrom0,
        const float* occProb,
        float* output,
        int W, int H,
        float alpha,
        cudaStream_t stream);

    bool ljLaunchFiveLevelBlend(
        const float* warpFrom1,
        const float* warpFrom0,
        const float* confidence,
        const unsigned char* level,
        const float* I0,
        const float* I1,
        float* output,
        int W, int H,
        float alpha,
        cudaStream_t stream);

    bool ljLaunchMultiFrameBlend(
        const float* multiWarpFrom1,
        const float* multiWarpFrom0,
        const float* occProb,
        float* multiOutput,
        int W, int H,
        int numFrames,
        float alphaStart,
        float alphaStep,
        cudaStream_t stream);

    bool ljLaunchTemporalSmooth(
        const float* currentInterp,
        const float* prevInterp,
        const float* occProb,
        float* smoothed,
        int W, int H,
        float historyWeight,
        cudaStream_t stream);
}
#endif

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    FrameBlender::FrameBlender() = default;
    FrameBlender::~FrameBlender() { shutdown(); }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool FrameBlender::initialize(IGpuContext* gpuContext,
        const BlendConfig& config)
    {
        if (initialized_) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("FrameBlender: invalid GPU context");
            return false;
        }

        gpuContext_ = gpuContext;
        config_ = config;
        initialized_ = true;

        LOG_INFO("FrameBlender initialized: mode=%u",
            static_cast<uint32_t>(config.mode));
        return true;
    }

    void FrameBlender::shutdown() {
        if (!initialized_) return;

        cachedOutput.reset();
        cachedSmoothed.reset();

        cachedWidth_ = 0;
        cachedHeight_ = 0;

        gpuContext_ = nullptr;
        initialized_ = false;
    }

    // ============================================================================
    // 缓冲管理
    // ============================================================================

    bool FrameBlender::ensureBuffers(uint32_t W, uint32_t H,
        bool multiFrame,
        uint32_t numFrames)
    {
        if (cachedWidth_ == W && cachedHeight_ == H && cachedOutput.valid()) {
            return true;
        }

        auto outBuf = gpuContext_->createTexture(W, H, TextureFormat::R32_FLOAT);
        auto smoothBuf = gpuContext_->createTexture(W, H, TextureFormat::R32_FLOAT);

        if (!outBuf || !smoothBuf) {
            return false;
        }

        cachedOutput.nativeHandle = outBuf->nativeHandle();
        cachedOutput.width = W;
        cachedOutput.height = H;
        cachedOutput.format = TextureFormat::R32_FLOAT;

        cachedSmoothed.nativeHandle = smoothBuf->nativeHandle();
        cachedSmoothed.width = W;
        cachedSmoothed.height = H;
        cachedSmoothed.format = TextureFormat::R32_FLOAT;

        cachedWidth_ = W;
        cachedHeight_ = H;

        return true;
    }

    // ============================================================================
    // 单帧混合
    // ============================================================================

    bool FrameBlender::blend(const GpuTexture& warpFrom0,
        const GpuTexture& warpFrom1,
        const GpuTexture& occProb,
        GpuTexture& output,
        float alpha)
    {
        if (!initialized_) return false;
        if (!warpFrom0.valid() || !warpFrom1.valid() || !occProb.valid()) {
            return false;
        }

        uint32_t W = warpFrom0.width;
        uint32_t H = warpFrom0.height;

        if (!ensureBuffers(W, H, false, 1)) return false;

#ifdef LJ_NVIDIA
        cudaStream_t stream = static_cast<cudaStream_t>(
            gpuContext_->defaultStream().nativeHandle());

        bool ok = ljLaunchOcclusionAwareBlend(
            static_cast<const float*>(warpFrom1.nativeHandle),
            static_cast<const float*>(warpFrom0.nativeHandle),
            static_cast<const float*>(occProb.nativeHandle),
            static_cast<float*>(cachedOutput.nativeHandle),
            static_cast<int>(W), static_cast<int>(H),
            alpha,
            stream);

        if (!ok) {
            LOG_ERROR("FrameBlender: blend kernel failed");
            return false;
        }

        output = cachedOutput;
        return true;
#else
        return false;
#endif
    }

    // ============================================================================
    // 五级混合
    // ============================================================================

    bool FrameBlender::blendFiveLevel(const GpuTexture& warpFrom0,
        const GpuTexture& warpFrom1,
        const GpuTexture& confidence,
        const GpuTexture& level,
        const GpuTexture& I0,
        const GpuTexture& I1,
        GpuTexture& output,
        float alpha)
    {
        if (!initialized_) return false;
        if (!warpFrom0.valid() || !warpFrom1.valid()) return false;
        if (!confidence.valid() || !level.valid()) return false;

        uint32_t W = warpFrom0.width;
        uint32_t H = warpFrom0.height;

        if (!ensureBuffers(W, H, false, 1)) return false;

#ifdef LJ_NVIDIA
        cudaStream_t stream = static_cast<cudaStream_t>(
            gpuContext_->defaultStream().nativeHandle());

        bool ok = ljLaunchFiveLevelBlend(
            static_cast<const float*>(warpFrom1.nativeHandle),
            static_cast<const float*>(warpFrom0.nativeHandle),
            static_cast<const float*>(confidence.nativeHandle),
            static_cast<const unsigned char*>(level.nativeHandle),
            static_cast<const float*>(I0.nativeHandle),
            static_cast<const float*>(I1.nativeHandle),
            static_cast<float*>(cachedOutput.nativeHandle),
            static_cast<int>(W), static_cast<int>(H),
            alpha,
            stream);

        if (!ok) {
            LOG_ERROR("FrameBlender: five level kernel failed");
            return false;
        }

        output = cachedOutput;
        return true;
#else
        return false;
#endif
    }

    // ============================================================================
    // 多帧混合
    // ============================================================================

    bool FrameBlender::blendMultiFrame(const GpuTexture& multiWarpFrom0,
        const GpuTexture& multiWarpFrom1,
        const GpuTexture& occProb,
        GpuTexture& multiOutput,
        uint32_t numFrames,
        float alphaStart,
        float alphaStep)
    {
        if (!initialized_) return false;
        if (!multiWarpFrom0.valid() || !multiWarpFrom1.valid()) return false;
        if (!occProb.valid()) return false;
        if (numFrames == 0) return false;

        uint32_t W = occProb.width;
        uint32_t H = occProb.height;

        // 多帧输出缓冲
        // 注：multiWarpFrom0/1 已是 [N][H][W] 布局，输出也是相同布局

        // 直接使用输出纹理（由调用方提供）
        if (!multiOutput.valid()) {
            return false;
        }

#ifdef LJ_NVIDIA
        cudaStream_t stream = static_cast<cudaStream_t>(
            gpuContext_->defaultStream().nativeHandle());

        bool ok = ljLaunchMultiFrameBlend(
            static_cast<const float*>(multiWarpFrom1.nativeHandle),
            static_cast<const float*>(multiWarpFrom0.nativeHandle),
            static_cast<const float*>(occProb.nativeHandle),
            static_cast<float*>(multiOutput.nativeHandle),
            static_cast<int>(W), static_cast<int>(H),
            static_cast<int>(numFrames),
            alphaStart,
            alphaStep,
            stream);

        return ok;
#else
        return false;
#endif
    }

    // ============================================================================
    // 时域平滑
    // ============================================================================

    bool FrameBlender::temporalSmooth(const GpuTexture& currentInterp,
        const GpuTexture& prevInterp,
        const GpuTexture& occProb,
        GpuTexture& smoothed)
    {
        if (!initialized_) return false;
        if (!currentInterp.valid() || !prevInterp.valid()) return false;

        uint32_t W = currentInterp.width;
        uint32_t H = currentInterp.height;

        if (!ensureBuffers(W, H, false, 1)) return false;

#ifdef LJ_NVIDIA
        cudaStream_t stream = static_cast<cudaStream_t>(
            gpuContext_->defaultStream().nativeHandle());

        bool ok = ljLaunchTemporalSmooth(
            static_cast<const float*>(currentInterp.nativeHandle),
            static_cast<const float*>(prevInterp.nativeHandle),
            static_cast<const float*>(occProb.nativeHandle),
            static_cast<float*>(cachedSmoothed.nativeHandle),
            static_cast<int>(W), static_cast<int>(H),
            config_.temporalWeight,
            stream);

        if (!ok) return false;

        smoothed = cachedSmoothed;
        return true;
#else
        return false;
#endif
    }

} // namespace Lingjing