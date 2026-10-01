#ifdef LJ_INTEL

#include "ai/OpenVINOAIREpair.h"
#include "ai/ModelLoader.h"
#include "ai/ModelValidator.h"
#include "core/Logger.h"

#include <vector>
#include <mutex>

namespace Lingjing {

    // ============================================================================
    // Impl
    // ============================================================================

    struct OpenVINOAIREpair::Impl {
        IGpuContext* gpuContext = nullptr;
        AIRepairConfig config;
        ModelMetadata metadata;
        bool initialized = false;

        std::unique_ptr<OpenVINOEngine> engine;

        // 主机缓冲
        std::vector<uint8_t> hostInputBuffer;
        std::vector<uint8_t> hostOutputBuffer;

        // 性能
        float totalInferenceMs = 0.0f;
        uint64_t inferenceCount = 0;

        std::mutex mutex;
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    OpenVINOAIREpair::OpenVINOAIREpair()
        : impl_(std::make_unique<Impl>()) {
    }

    OpenVINOAIREpair::~OpenVINOAIREpair() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool OpenVINOAIREpair::initialize(IGpuContext* gpuContext,
        const AIRepairConfig& config)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (impl_->initialized) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("OpenVINOAIREpair: invalid GPU context");
            return false;
        }

        if (gpuContext->vendor() != GpuVendor::Intel) {
            LOG_ERROR("OpenVINOAIREpair: requires Intel GPU");
            return false;
        }

        impl_->gpuContext = gpuContext;
        impl_->config = config;

        // 校验模型
        auto validation = ModelValidator::validate(config.modelPath, config);

        if (!validation.valid) {
            LOG_ERROR("OpenVINOAIREpair: model validation failed: %s",
                validation.errorMessage.c_str());
            return false;
        }

        impl_->metadata = validation.metadata;

        // 创建引擎
        impl_->engine = std::make_unique<OpenVINOEngine>();

        std::string device = "GPU";

        if (config.backend == AIRepairBackend::OpenVINO) {
            // 使用 OpenVINO GPU 设备
            device = "GPU";
        }

        bool enableFp16 = (config.precision == ModelPrecision::FP16);

        auto err = impl_->engine->loadModel(config.modelPath, device, enableFp16);

        if (err.isFailure()) {
            LOG_ERROR("OpenVINOAIREpair: load model failed: %s",
                err.message.c_str());
            return false;
        }

        impl_->initialized = true;

        LOG_INFO("OpenVINOAIREpair initialized");
        return true;
    }

    // ============================================================================
    // 关闭
    // ============================================================================

    void OpenVINOAIREpair::shutdown() {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->initialized) return;

        if (impl_->engine) {
            impl_->engine->destroy();
            impl_->engine.reset();
        }

        impl_->hostInputBuffer.clear();
        impl_->hostOutputBuffer.clear();

        impl_->initialized = false;

        LOG_INFO("OpenVINOAIREpair shutdown");
    }

    // ============================================================================
    // 修复
    // ============================================================================

    bool OpenVINOAIREpair::repair(const GpuTexture& input,
        AIRepairResult& result)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->initialized || !impl_->engine ||
            !impl_->engine->isReady())
        {
            return false;
        }

        if (!input.valid()) {
            LOG_ERROR("OpenVINOAIREpair: invalid input");
            return false;
        }

        result.reset();

        // 1. 拷贝输入到主机（简化实现）
        // 完整实现应使用 SYCL 零拷贝

        // 这里假设输入已经在主机内存中
        // 实际使用时应通过 interop 直接从 GPU 内存获取

        size_t inputBytes = impl_->engine->inputShape(0).empty()
            ? 0 : 1;

        // 设置输入
        auto err = impl_->engine->setInput(0,
            input.nativeHandle,
            inputBytes);

        if (err.isFailure()) {
            LOG_ERROR("Set input failed: %s", err.message.c_str());
            return false;
        }

        // 2. 推理
        err = impl_->engine->infer();

        if (err.isFailure()) {
            LOG_ERROR("Inference failed: %s", err.message.c_str());
            return false;
        }

        result.inferenceTimeMs = impl_->engine->lastInferenceTimeMs();

        impl_->totalInferenceMs += result.inferenceTimeMs;
        impl_->inferenceCount++;

        // 3. 构造输出
        result.output = input;
        result.success = true;

        return true;
    }

    bool OpenVINOAIREpair::repairBatch(
        const std::vector<GpuTexture>& inputs,
        std::vector<AIRepairResult>& results)
    {
        results.clear();
        results.reserve(inputs.size());

        for (const auto& input : inputs) {
            AIRepairResult result;

            if (!repair(input, result)) {
                results.push_back(std::move(result));
                return false;
            }

            results.push_back(std::move(result));
        }

        return true;
    }

    // ============================================================================
    // 查询
    // ============================================================================

    bool OpenVINOAIREpair::isReady() const {
        return impl_->initialized &&
            impl_->engine &&
            impl_->engine->isReady();
    }

    AIRepairBackend OpenVINOAIREpair::backend() const {
        return AIRepairBackend::OpenVINO;
    }

    const char* OpenVINOAIREpair::engineName() const {
        return "OpenVINO AI Repair";
    }

    const ModelMetadata& OpenVINOAIREpair::metadata() const {
        return impl_->metadata;
    }

    float OpenVINOAIREpair::averageInferenceTimeMs() const {
        if (impl_->inferenceCount == 0) return 0.0f;
        return impl_->totalInferenceMs /
            static_cast<float>(impl_->inferenceCount);
    }

    size_t OpenVINOAIREpair::memoryUsageBytes() const {
        return impl_->hostInputBuffer.size() +
            impl_->hostOutputBuffer.size();
    }

    // ============================================================================
    // 重载
    // ============================================================================

    bool OpenVINOAIREpair::reloadModel(const std::string& newModelPath) {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->initialized) return false;

        if (impl_->engine) {
            impl_->engine->destroy();
            impl_->engine.reset();
        }

        impl_->config.modelPath = newModelPath;

        IGpuContext* ctx = impl_->gpuContext;
        AIRepairConfig cfg = impl_->config;

        impl_->initialized = false;
        lock.~lock_guard();

        return initialize(ctx, cfg);
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IAIRepair> createOpenVINOAIREpair() {
        return std::make_unique<OpenVINOAIREpair>();
    }

} // namespace Lingjing
#endif