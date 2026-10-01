#pragma once

#include "core/Types.h"
#include "gpu/IGpuContext.h"
#include <memory>

namespace Lingjing {

    // ============================================================================
    // 遮挡处理结果
    // ============================================================================

    struct OcclusionResult {
        GpuTexture occlusionProb;      // 遮挡概率图 (R32F)
        GpuTexture occlusionMask;      // 硬遮挡掩码 (R8)
        float occlusionRatio = 0.0f;   // 遮挡像素比例
        float averageConfidence = 0.0f;

        bool valid() const { return occlusionProb.valid(); }
    };

    // ============================================================================
    // 遮挡处理配置
    // ============================================================================

    struct OcclusionConfig {
        // 一致性阈值
        float consistencyThreshold = 0.55f;

        // 边界硬化
        bool enableBoundaryHardening = true;
        float boundaryGradientThreshold = 0.05f;

        // 修补
        bool enableInfill = true;
        int infillRadius = 6;
        float infillOccThreshold = 0.4f;

        // 多尺度
        bool enableMultiscale = true;
    };

    // ============================================================================
    // 遮挡处理器
    // ============================================================================

    class OcclusionHandler {
    public:
        OcclusionHandler();
        ~OcclusionHandler();

        bool initialize(IGpuContext* gpuContext,
            const OcclusionConfig& config);

        void shutdown();

        // 计算遮挡概率
        bool computeOcclusion(const GpuTexture& fwdFlow,
            const GpuTexture& bwdFlow,
            OcclusionResult& result);

        // 修补光流（对遮挡区域）
        bool infillFlow(const GpuTexture& flowIn,
            const GpuTexture& occProb,
            const GpuTexture& guideLuma,
            GpuTexture& flowOut);

        // 查询
        bool isReady() const { return initialized_; }

    private:
        IGpuContext* gpuContext_ = nullptr;
        OcclusionConfig config_;
        bool initialized_ = false;

        // 中间缓冲
        GpuTexture cachedErrorMap;
        GpuTexture cachedOccProb;
        GpuTexture cachedOccMask;
        GpuTexture cachedInfillFlow;

        uint32_t cachedWidth_ = 0;
        uint32_t cachedHeight_ = 0;

        bool ensureBuffers(uint32_t W, uint32_t H);
    };

} // namespace Lingjing