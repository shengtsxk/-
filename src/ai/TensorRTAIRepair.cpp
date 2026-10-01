#ifdef LJ_NVIDIA

#include "ai/TensorRTAIRepair.h"
#include "ai/ModelLoader.h"
#include "ai/ModelValidator.h"
#include "core/Logger.h"
#include "core/Timer.h"

#include <cuda_runtime.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace Lingjing {

    // ============================================================================
    // Impl
    // ============================================================================

    struct TensorRTAIRepair::Impl {
        IGpuContext* gpuContext = nullptr;
        AIRepairConfig config;
        ModelMetadata metadata;
        bool initialized = false;

        std::unique_ptr<TensorRTEngine> engine;

        // 设备缓冲
        void* d_inputLuma = nullptr;
        void* d_inputColor = nullptr;
        void* d_output = nullptr;

        size_t inputLumaBytes = 0;
        size_t inputColorBytes = 0;
        size_t outputBytes = 0;

        // 缓存的引擎文件路径
        std::string cachedEnginePath;

        // 性能统计
        float totalInferenceMs = 0.0f;
        uint64_t inferenceCount = 0;

        std::mutex mutex;

        // 辅助
        bool ensureBuffers(uint32_t width, uint32_t height);
        void freeBuffers();
        std::string getEngineCachePath(const std::string& modelPath);
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    TensorRTAIRepair::TensorRTAIRepair()
        : impl_(std::make_unique<Impl>()) {
    }

    TensorRTAIRepair::~TensorRTAIRepair() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool TensorRTAIRepair::initialize(IGpuContext* gpuContext,
        const AIRepairConfig& config)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (impl_->initialized) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("TensorRTAIRepair: invalid GPU context");
            return false;
        }

        if (gpuContext->vendor() != GpuVendor::NVIDIA) {
            LOG_ERROR("TensorRTAIRepair: requires NVIDIA GPU");
            return false;
        }

        impl_->gpuContext = gpuContext;
        impl_->config = config;

        // 校验模型
        auto validation = ModelValidator::validate(config.modelPath, config);

        if (!validation.valid) {
            LOG_ERROR("TensorRTAIRepair: model validation failed: %s",
                validation.errorMessage.c_str());

            for (const auto& sug : validation.suggestions) {
                LOG_INFO("  Suggestion: %s", sug.c_str());
            }

            return false;
        }

        impl_->metadata = validation.metadata;

        // 创建引擎
        impl_->engine = std::make_unique<TensorRTEngine>();

        // 检查引擎缓存
        std::string cachePath = impl_->getEngineCachePath(config.modelPath);

        bool loadedFromCache = false;

        if (config.enableEngineCache &&
            !cachePath.empty() &&
            fs::exists(cachePath))
        {
            LOG_INFO("TensorRTAIRepair: loading cached engine from %s",
                cachePath.c_str());

            auto err = impl_->engine->loadFromEngineFile(cachePath);

            if (err.isSuccess()) {
                loadedFromCache = true;
            }
            else {
                LOG_WARN("Failed to load cached engine, rebuilding: %s",
                    err.message.c_str());
            }
        }

        // 从 ONNX 构建引擎
        if (!loadedFromCache) {
            LOG_INFO("TensorRTAIRepair: building engine from %s",
                config.modelPath.c_str());

            auto err = impl_->engine->buildFromONNX(config.modelPath, config);

            if (err.isFailure()) {
                LOG_ERROR("Failed to build engine: %s", err.message.c_str());
                return false;
            }

            // 保存到缓存
            if (config.enableEngineCache && !cachePath.empty()) {
                try {
                    fs::create_directories(fs::path(cachePath).parent_path());

                    auto saveErr = impl_->engine->saveEngine(cachePath);

                    if (saveErr.isSuccess()) {
                        LOG_INFO("Engine cached to %s", cachePath.c_str());
                    }
                }
                catch (const std::exception& e) {
                    LOG_WARN("Failed to cache engine: %s", e.what());
                }
            }
        }

        impl_->initialized = true;

        LOG_INFO("TensorRTAIRepair initialized");
        return true;
    }

    // ============================================================================
    // 引擎缓存路径
    // ============================================================================

    std::string TensorRTAIRepair::Impl::getEngineCachePath(
        const std::string& modelPath)
    {
        if (!config.enableEngineCache) return "";

        std::string cacheDir = config.cacheDir;

        if (cacheDir.empty()) {
            cacheDir = ModelLoader::getModelCacheDirectory();
        }

        // 基于模型路径 + 配置的哈希
        std::string hashInput = modelPath +
            std::to_string(config.enableFp16) +
            std::to_string(config.enableInt8) +
            std::to_string(config.enableBF16) +
            std::to_string(config.maxBatchSize);

        // 简化：使用 FNV-1a 哈希
        uint64_t hash = 14695981039346656037ULL;
        for (char c : hashInput) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "%016llx.trt",
            static_cast<unsigned long long>(hash));

        std::string modelName = fs::path(modelPath).stem().string();

        return (fs::path(cacheDir) / (modelName + "_" + buf)).string();
    }

    // ============================================================================
    // 缓冲分配
    // ============================================================================

    bool TensorRTAIRepair::Impl::ensureBuffers(uint32_t width, uint32_t height) {
        size_t lumaBytes = static_cast<size_t>(width) * height * sizeof(float);
        size_t colorBytes = static_cast<size_t>(width) * height * 3 * sizeof(float);
        size_t outputSz = static_cast<size_t>(width) * height * sizeof(float);

        if (inputLumaBytes == lumaBytes &&
            inputColorBytes == colorBytes &&
            outputBytes == outputSz)
        {
            return true;
        }

        freeBuffers();

        cudaError_t err;

        err = cudaMalloc(&d_inputLuma, lumaBytes);
        if (err != cudaSuccess) return false;

        err = cudaMalloc(&d_inputColor, colorBytes);
        if (err != cudaSuccess) {
            freeBuffers();
            return false;
        }

        err = cudaMalloc(&d_output, outputSz);
        if (err != cudaSuccess) {
            freeBuffers();
            return false;
        }

        inputLumaBytes = lumaBytes;
        inputColorBytes = colorBytes;
        outputBytes = outputSz;

        return true;
    }

    void TensorRTAIRepair::Impl::freeBuffers() {
        if (d_inputLuma) { cudaFree(d_inputLuma); d_inputLuma = nullptr; }
        if (d_inputColor) { cudaFree(d_inputColor); d_inputColor = nullptr; }
        if (d_output) { cudaFree(d_output); d_output = nullptr; }

        inputLumaBytes = 0;
        inputColorBytes = 0;
        outputBytes = 0;
    }

    // ============================================================================
    // 修复
    // ============================================================================

    bool TensorRTAIRepair::repair(const GpuTexture& input,
        AIRepairResult& result)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->initialized || !impl_->engine ||
            !impl_->engine->isReady())
        {
            return false;
        }

        if (!input.valid()) {
            LOG_ERROR("TensorRTAIRepair: invalid input");
            return false;
        }

        result.reset();

        Timer totalTimer;
        totalTimer.start();

        uint32_t W = input.width;
        uint32_t H = input.height;

        // 分配缓冲
        if (!impl_->ensureBuffers(W, H)) {
            LOG_ERROR("TensorRTAIRepair: buffer allocation failed");
            return false;
        }

        // 获取默认流
        cudaStream_t stream = static_cast<cudaStream_t>(
            impl_->gpuContext->defaultStream().nativeHandle());

        // 1. 预处理：灰度转浮点（具体取决于模型输入要求）
        // 这里假设输入已经是正确的格式

        // 2. 设置输入
        auto inputNames = impl_->engine->inputNames();
        auto outputNames = impl_->engine->outputNames();

        if (inputNames.empty() || outputNames.empty()) {
            LOG_ERROR("TensorRTAIRepair: no bindings");
            return false;
        }

        // 具体绑定取决于模型
        // 这里假设单输入单输出

        auto err = impl_->engine->setInput(inputNames[0],
            impl_->d_inputLuma,
            impl_->inputLumaBytes);
        if (err.isFailure()) {
            LOG_ERROR("Failed to set input: %s", err.message.c_str());
            return false;
        }

        err = impl_->engine->setOutput(outputNames[0],
            impl_->d_output,
            impl_->outputBytes);
        if (err.isFailure()) {
            LOG_ERROR("Failed to set output: %s", err.message.c_str());
            return false;
        }

        // 3. 执行推理
        err = impl_->engine->infer(stream);

        if (err.isFailure()) {
            LOG_ERROR("Inference failed: %s", err.message.c_str());
            return false;
        }

        result.inferenceTimeMs = impl_->engine->lastInferenceTimeMs();

        // 更新统计
        impl_->totalInferenceMs += result.inferenceTimeMs;
        impl_->inferenceCount++;

        // 4. 构造输出纹理
        result.output = input;  // 复用输入描述
        result.output.nativeHandle = impl_->d_output;

        result.success = true;
        result.preprocessTimeMs = 0.0f;
        result.postprocessTimeMs = 0.0f;

        return true;
    }

    bool TensorRTAIRepair::repairBatch(
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
    // 关闭
    // ============================================================================

    void TensorRTAIRepair::shutdown() {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->initialized) return;

        impl_->freeBuffers();

        if (impl_->engine) {
            impl_->engine->destroy();
            impl_->engine.reset();
        }

        impl_->initialized = false;

        LOG_INFO("TensorRTAIRepair shutdown");
    }

    // ============================================================================
    // 查询
    // ============================================================================

    bool TensorRTAIRepair::isReady() const {
        return impl_->initialized &&
            impl_->engine &&
            impl_->engine->isReady();
    }

    AIRepairBackend TensorRTAIRepair::backend() const {
        return AIRepairBackend::TensorRT;
    }

    const char* TensorRTAIRepair::engineName() const {
        return "TensorRT AI Repair";
    }

    const ModelMetadata& TensorRTAIRepair::metadata() const {
        return impl_->metadata;
    }

    float TensorRTAIRepair::averageInferenceTimeMs() const {
        if (impl_->inferenceCount == 0) return 0.0f;
        return impl_->totalInferenceMs /
            static_cast<float>(impl_->inferenceCount);
    }

    size_t TensorRTAIRepair::memoryUsageBytes() const {
        return impl_->inputLumaBytes +
            impl_->inputColorBytes +
            impl_->outputBytes;
    }

    // ============================================================================
    // 重新加载
    // ============================================================================

    bool TensorRTAIRepair::reloadModel(const std::string& newModelPath) {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->initialized) return false;

        // 清理旧引擎
        if (impl_->engine) {
            impl_->engine->destroy();
            impl_->engine.reset();
        }

        // 更新配置
        impl_->config.modelPath = newModelPath;

        // 重新初始化
        IGpuContext* ctx = impl_->gpuContext;
        AIRepairConfig cfg = impl_->config;

        impl_->initialized = false;

        // 释放锁后重新初始化（避免死锁）
        lock.~lock_guard();

        return initialize(ctx, cfg);
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IAIRepair> createTensorRTAIRepair() {
        return std::make_unique<TensorRTAIRepair>();
    }

} // namespace Lingjing
#endif