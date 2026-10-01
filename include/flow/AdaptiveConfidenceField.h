#pragma once

#include "core/Types.h"
#include "gpu/IGpuContext.h"
#include "flow/Coherent3DFlow.h"
#include <memory>

namespace Lingjing {

    // ============================================================================
    // 融合结果
    // ============================================================================

    struct AdaptiveConfidenceResult {
        GpuTexture finalFlow;
        GpuTexture confidence;
        uint32_t width = 0;
        uint32_t height = 0;

        bool isValid() const { return finalFlow.valid(); }
    };

    // ============================================================================
    // 自适应可信度场
    // ============================================================================

    class AdaptiveConfidenceField {
    public:
        AdaptiveConfidenceField();
        ~AdaptiveConfidenceField();

        bool initialize(IGpuContext* gpuContext);
        void shutdown();

        // 融合几何流与 2D 光流
        bool fuse(const GpuTexture& geoFlow,
            const GpuTexture& geoConsistency,
            const GpuTexture& flowFlow,
            const GpuTexture& flowConfidence,
            const GpuTexture& depth,
            AdaptiveConfidenceResult& result);

        // 时域平滑
        bool temporalSmooth(const GpuTexture& currentFlow,
            const GpuTexture& prevFlow,
            const GpuTexture& flowConfidence,
            const GpuTexture& depth,
            GpuTexture& outFlow,
            float temporalWeight);

        bool isReady() const { return initialized_; }

    private:
        IGpuContext* gpuContext_ = nullptr;
        bool initialized_ = false;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing