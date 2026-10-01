#pragma once

#include "core/Types.h"
#include "gpu/IGpuContext.h"
#include "flow/IFlowEngine.h"
#include "pipeline/OcclusionHandler.h"
#include "pipeline/FrameBlender.h"
#include "ai/IAIRepair.h"

#include <memory>
#include <vector>

namespace Lingjing {

    class D3D11FrameInterpolator;

    // ============================================================================
    // 多帧生成配置
    // ============================================================================

    struct MultiFrameConfig {
        uint32_t multiplier = 2;         // 1~20 倍
        float alphaStart = 0.5f;         // 起始 alpha（单帧时固定为 0.5）
        float alphaEnd = 0.5f;           // 结束 alpha

        // AI 修复
        bool enableAIRepair = false;
        uint32_t aiRepairEveryNthFrame = 1;  // 每 N 帧做一次 AI 修复

        // 时域平滑
        bool enableTemporalSmoothing = true;

        // 输出格式
        bool outputAsBgra = true;        // true: BGRA, false: 单通道
    };

    // ============================================================================
    // 多帧生成结果
    // ============================================================================

    struct MultiFrameResult {
        std::vector<GpuTexture> frames;   // 生成的 N 帧
        uint32_t numFrames = 0;

        // 性能
        float totalMs = 0.0f;
        float flowMs = 0.0f;
        float occlusionMs = 0.0f;
        float blendMs = 0.0f;
        float aiMs = 0.0f;

        // 质量
        float avgQuality = 0.0f;

        void reset() {
            frames.clear();
            numFrames = 0;
            totalMs = 0.0f;
            flowMs = 0.0f;
            occlusionMs = 0.0f;
            blendMs = 0.0f;
            aiMs = 0.0f;
            avgQuality = 0.0f;
        }
    };

    // ============================================================================
    // 多帧生成器
    // ============================================================================

    class MultiFrameGenerator {
    public:
        MultiFrameGenerator();
        ~MultiFrameGenerator();

        bool initialize(IGpuContext* gpuContext,
            IFlowEngine* flowEngine,
            OcclusionHandler* occlusionHandler,
            FrameBlender* frameBlender,
            IAIRepair* aiRepair);

        void shutdown();

        // 生成 N 帧
        bool generateFrames(const GpuTexture& frame0,
            const GpuTexture& frame1,
            const GpuTexture& prevFrame,
            const MultiFrameConfig& config,
            MultiFrameResult& result);

        // 重置
        void reset();

        // 查询
        bool isReady() const { return initialized_; }

    private:
        IGpuContext* gpuContext_ = nullptr;
        IFlowEngine* flowEngine_ = nullptr;
        OcclusionHandler* occlusionHandler_ = nullptr;
        FrameBlender* frameBlender_ = nullptr;
        IAIRepair* aiRepair_ = nullptr;

        bool initialized_ = false;

        // D3D11 插帧核心（真实实现）
        std::unique_ptr<D3D11FrameInterpolator> d3d11_;

        // 输出帧池（固定大容量，永不 realloc，Presenter 异步读安全）
        std::vector<std::vector<uint8_t>> framePool_;

        // 缓存
        GpuTexture cachedMultiWarp0_;
        GpuTexture cachedMultiWarp1_;
        GpuTexture cachedMultiOutput_;
        uint32_t cachedWidth_ = 0;
        uint32_t cachedHeight_ = 0;
        uint32_t cachedNumFrames_ = 0;

        // 上一帧的中间帧（用于时域平滑）
        std::vector<GpuTexture> prevFrames_;
        bool hasPrevFrames_ = false;

        // 辅助
        bool ensureMultiFrameBuffers(uint32_t W, uint32_t H,
            uint32_t N);
        bool runMultiFrameWarp(const GpuTexture& I0,
            const GpuTexture& I1,
            const GpuTexture& fwdFlow,
            const GpuTexture& bwdFlow,
            uint32_t numFrames,
            float alphaStart,
            float alphaStep);
        bool runAIRepair(const MultiFrameConfig& config,
            MultiFrameResult& result);
    };

} // namespace Lingjing