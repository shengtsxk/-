#pragma once

#include "flow/IFlowEngine.h"
#include "flow/TimeGeometryMemory.h"
#include "flow/MultiCueProjection.h"
#include "flow/Coherent3DFlow.h"
#include "flow/AdaptiveConfidenceField.h"

#include <memory>

namespace Lingjing {

    // ============================================================================
    // 淮竹引擎
    // ============================================================================

    class HuaiZhuEngine : public IFlowEngine {
    public:
        HuaiZhuEngine();
        ~HuaiZhuEngine() override;

        bool initialize(IGpuContext* gpuContext,
            const FlowSolveConfig& config) override;
        void shutdown() override;

        bool solve(const GpuTexture& prevFrame,
            const GpuTexture& currFrame,
            FlowResult& result) override;

        void reset() override;

        bool isReady() const override;
        FlowEngineType type() const override;
        const char* engineName() const override;
        GpuVendor vendor() const override;

        float convergence() const override;
        bool isWarmedUp() const override;

        void setLearningContext(void* context) override;
        void* learningContext() const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing