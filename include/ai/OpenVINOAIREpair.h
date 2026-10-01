#pragma once
#ifdef LJ_INTEL

#include "ai/IAIRepair.h"
#include "ai/OpenVINOEngine.h"

#include <memory>

namespace Lingjing {

    class OpenVINOAIREpair : public IAIRepair {
    public:
        OpenVINOAIREpair();
        ~OpenVINOAIREpair() override;

        bool initialize(IGpuContext* gpuContext,
            const AIRepairConfig& config) override;

        void shutdown() override;

        bool repair(const GpuTexture& input,
            AIRepairResult& result) override;

        bool repairBatch(const std::vector<GpuTexture>& inputs,
            std::vector<AIRepairResult>& results) override;

        bool isReady() const override;
        AIRepairBackend backend() const override;
        const char* engineName() const override;

        const ModelMetadata& metadata() const override;

        float averageInferenceTimeMs() const override;
        size_t memoryUsageBytes() const override;

        bool reloadModel(const std::string& newModelPath) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing
#endif