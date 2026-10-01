#pragma once

#include "core/Types.h"
#include "gpu/IGpuContext.h"
#include <memory>

namespace Lingjing {

    // ============================================================================
    // 混合模式
    // ============================================================================

    enum class BlendMode : uint32_t {
        Linear = 0,        // 线性混合
        OcclusionAware,    // 遮挡感知
        FiveLevel,         // 五级可信度
        Adaptive,          // 自适应
    };

    // ============================================================================
    // 混合配置
    // ============================================================================

    struct BlendConfig {
        BlendMode mode = BlendMode::OcclusionAware;

        // 遮挡感知权重偏置
        float occlusionBias = 1.0f;

        // 时域平滑
        bool enableTemporalSmoothing = true;
        float temporalWeight = 0.15f;

        // 边界硬化
        bool enableBoundaryHardening = true;
    };

    // ============================================================================
    // 帧混合器
    // ============================================================================

    class FrameBlender {
    public:
        FrameBlender();
        ~FrameBlender();

        bool initialize(IGpuContext* gpuContext,
            const BlendConfig& config);

        void shutdown();

        // ====================================================================
        // 单帧混合
        // ====================================================================

        bool blend(const GpuTexture& warpFrom0,
            const GpuTexture& warpFrom1,
            const GpuTexture& occProb,
            GpuTexture& output,
            float alpha);

        // 五级混合
        bool blendFiveLevel(const GpuTexture& warpFrom0,
            const GpuTexture& warpFrom1,
            const GpuTexture& confidence,
            const GpuTexture& level,
            const GpuTexture& I0,
            const GpuTexture& I1,
            GpuTexture& output,
            float alpha);

        // ====================================================================
        // 多帧混合
        // ====================================================================

        bool blendMultiFrame(const GpuTexture& multiWarpFrom0,
            const GpuTexture& multiWarpFrom1,
            const GpuTexture& occProb,
            GpuTexture& multiOutput,
            uint32_t numFrames,
            float alphaStart,
            float alphaStep);

        // ====================================================================
        // 时域平滑
        // ====================================================================

        bool temporalSmooth(const GpuTexture& currentInterp,
            const GpuTexture& prevInterp,
            const GpuTexture& occProb,
            GpuTexture& smoothed);

        bool isReady() const { return initialized_; }

    private:
        IGpuContext* gpuContext_ = nullptr;
        BlendConfig config_;
        bool initialized_ = false;

        // 缓存
        GpuTexture cachedOutput;
        GpuTexture cachedSmoothed;
        uint32_t cachedWidth_ = 0;
        uint32_t cachedHeight_ = 0;

        bool ensureBuffers(uint32_t W, uint32_t H, bool multiFrame,
            uint32_t numFrames);
    };

} // namespace Lingjing