#ifdef LJ_NVIDIA

#include "ai/TensorRTEngine.h"
#include "core/Logger.h"

#include <NvOnnxParser.h>
#include <fstream>
#include <cstring>

namespace Lingjing {

    // ============================================================================
    // TRTLogger
    // ============================================================================

    void TRTLogger::log(Severity severity, const char* msg) noexcept {
        switch (severity) {
        case Severity::kINTERNAL_ERROR:
        case Severity::kERROR:
            LOG_ERROR("[TRT] %s", msg);
            break;
        case Severity::kWARNING:
            LOG_WARN("[TRT] %s", msg);
            break;
        case Severity::kINFO:
            LOG_INFO("[TRT] %s", msg);
            break;
        case Severity::kVERBOSE:
            LOG_TRACE("[TRT] %s", msg);
            break;
        }
    }

    // ============================================================================
    // 构造/析构
    // ============================================================================

    TensorRTEngine::TensorRTEngine()
        : logger_(std::make_unique<TRTLogger>())
    {
        cudaEventCreate(&startEvent_);
        cudaEventCreate(&stopEvent_);
    }

    TensorRTEngine::~TensorRTEngine() {
        destroy();

        if (startEvent_) cudaEventDestroy(startEvent_);
        if (stopEvent_) cudaEventDestroy(stopEvent_);
    }

    // ============================================================================
    // 销毁
    // ============================================================================

    void TensorRTEngine::destroy() {
        std::lock_guard<std::mutex> lock(mutex_);

        clearBindings();

        context_.reset();
        engine_.reset();
        runtime_.reset();

        initialized_ = false;
    }

    void TensorRTEngine::clearBindings() {
        bindings_.clear();
        inputNames_.clear();
        outputNames_.clear();
    }

    // ============================================================================
    // 从 ONNX 构建
    // ============================================================================

    Error TensorRTEngine::buildFromONNX(const std::string& onnxPath,
        const AIRepairConfig& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        config_ = config;

        // 创建 builder
        auto builder = std::unique_ptr<nvinfer1::IBuilder>(
            nvinfer1::createInferBuilder(*logger_));

        if (!builder) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create TensorRT builder");
        }

        // 创建网络（显式批处理）
        const auto explicitBatch = 1U << static_cast<uint32_t>(
            nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);

        auto network = std::unique_ptr<nvinfer1::INetworkDefinition>(
            builder->createNetworkV2(explicitBatch));

        if (!network) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create TensorRT network");
        }

        // 创建 ONNX 解析器
        auto parser = std::unique_ptr<nvonnxparser::IParser>(
            nvonnxparser::createParser(*network, *logger_));

        if (!parser) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create ONNX parser");
        }

        // 解析 ONNX
        if (!parser->parseFromFile(onnxPath.c_str(),
            static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
            return Error(ErrorCode::ModelInvalid,
                "Failed to parse ONNX file: " + onnxPath);
        }

        // 创建 builder config
        auto builderConfig = std::unique_ptr<nvinfer1::IBuilderConfig>(
            builder->createBuilderConfig());

        if (!builderConfig) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create builder config");
        }

        // 设置工作空间
        builderConfig->setMemoryPoolLimit(
            nvinfer1::MemoryPoolType::kWORKSPACE,
            config.workspaceSizeBytes);

        // 设置精度
        if (config.enableFp16 && builder->platformHasFastFp16()) {
            builderConfig->setFlag(nvinfer1::BuilderFlag::kFP16);
            LOG_INFO("TensorRT: FP16 enabled");
        }

        if (config.enableInt8 && builder->platformHasFastInt8()) {
            builderConfig->setFlag(nvinfer1::BuilderFlag::kINT8);
            LOG_INFO("TensorRT: INT8 enabled");
        }

        if (config.enableBF16 && builder->platformHasBF16()) {
            builderConfig->setFlag(nvinfer1::BuilderFlag::kBF16);
            LOG_INFO("TensorRT: BF16 enabled");
        }

        // 动态 shape 支持
        if (config.enableDynamicShape) {
            auto profile = builder->createOptimizationProfile();

            if (profile) {
                // 为每个输入设置 shape
                for (int i = 0; i < network->getNbInputs(); ++i) {
                    auto input = network->getInput(i);
                    const char* name = input->getName();
                    auto dims = input->getDimensions();

                    // 创建最小/最优/最大 shape
                    nvinfer1::Dims minDims = dims;
                    nvinfer1::Dims optDims = dims;
                    nvinfer1::Dims maxDims = dims;

                    // 设置 batch 维度
                    minDims.d[0] = config.minBatchSize;
                    optDims.d[0] = config.optBatchSize;
                    maxDims.d[0] = config.maxBatch;

                    profile->setDimensions(name,
                        nvinfer1::OptProfileSelector::kMIN,
                        minDims);
                    profile->setDimensions(name,
                        nvinfer1::OptProfileSelector::kOPT,
                        optDims);
                    profile->setDimensions(name,
                        nvinfer1::OptProfileSelector::kMAX,
                        maxDims);
                }

                builderConfig->addOptimizationProfile(profile);
            }
        }

        // 序列化引擎
        auto serialized = std::unique_ptr<nvinfer1::IHostMemory>(
            builder->buildSerializedNetwork(*network, *builderConfig));

        if (!serialized) {
            return Error(ErrorCode::ModelLoadFailed,
                "TensorRT engine build failed");
        }

        LOG_INFO("TensorRT engine built: %.2f MB",
            static_cast<double>(serialized->size()) / (1024.0 * 1024.0));

        // 创建运行时和引擎
        runtime_.reset(nvinfer1::createInferRuntime(*logger_));

        if (!runtime_) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create TensorRT runtime");
        }

        engine_.reset(runtime_->deserializeCudaEngine(
            serialized->data(), serialized->size()));

        if (!engine_) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to deserialize TensorRT engine");
        }

        // 创建执行上下文
        context_.reset(engine_->createExecutionContext());

        if (!context_) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create TensorRT execution context");
        }

        // 设置绑定信息
        setupBindings();

        initialized_ = true;

        return Error{};
    }

    // ============================================================================
    // 从引擎文件加载
    // ============================================================================

    Error TensorRTEngine::loadFromEngineFile(const std::string& enginePath) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 读取引擎文件
        std::ifstream file(enginePath, std::ios::binary);
        if (!file.is_open()) {
            return Error(ErrorCode::FileNotFound,
                "Cannot open engine file: " + enginePath);
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> data(size);
        file.read(data.data(), size);
        file.close();

        // 创建运行时
        runtime_.reset(nvinfer1::createInferRuntime(*logger_));

        if (!runtime_) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create TensorRT runtime");
        }

        // 反序列化
        engine_.reset(runtime_->deserializeCudaEngine(data.data(), size));

        if (!engine_) {
            return Error(ErrorCode::ModelInvalid,
                "Failed to deserialize TensorRT engine");
        }

        // 创建上下文
        context_.reset(engine_->createExecutionContext());

        if (!context_) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to create execution context");
        }

        setupBindings();

        initialized_ = true;

        LOG_INFO("TensorRT engine loaded from: %s", enginePath.c_str());

        return Error{};
    }

    // ============================================================================
    // 保存引擎
    // ============================================================================

    Error TensorRTEngine::saveEngine(const std::string& enginePath) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!engine_) {
            return Error(ErrorCode::ModelLoadFailed, "Engine not initialized");
        }

        auto serialized = std::unique_ptr<nvinfer1::IHostMemory>(
            engine_->serialize());

        if (!serialized) {
            return Error(ErrorCode::ModelLoadFailed,
                "Failed to serialize engine");
        }

        std::ofstream file(enginePath, std::ios::binary);
        if (!file.is_open()) {
            return Error(ErrorCode::PermissionDenied,
                "Cannot write engine file: " + enginePath);
        }

        file.write(static_cast<const char*>(serialized->data()),
            serialized->size());

        LOG_INFO("TensorRT engine saved: %s", enginePath.c_str());

        return Error{};
    }

    // ============================================================================
    // 绑定设置
    // ============================================================================

    void TensorRTEngine::setupBindings() {
        clearBindings();

        int numBindings = engine_->getNbIOTensors();

        for (int i = 0; i < numBindings; ++i) {
            const char* name = engine_->getIOTensorName(i);
            auto mode = engine_->getTensorIOMode(name);
            auto dataType = engine_->getTensorDataType(name);
            auto dims = engine_->getTensorShape(name);

            BindingInfo info;
            info.name = name;
            info.mode = mode;
            info.dataType = dataType;
            info.dims = dims;

            // 计算字节数
            size_t elements = 1;
            for (int d = 0; d < dims.nbDims; ++d) {
                if (dims.d[d] > 0) {
                    elements *= static_cast<size_t>(dims.d[d]);
                }
            }

            size_t elementSize = 4;
            switch (dataType) {
            case nvinfer1::DataType::kFLOAT: elementSize = 4; break;
            case nvinfer1::DataType::kHALF:  elementSize = 2; break;
            case nvinfer1::DataType::kINT8:  elementSize = 1; break;
            case nvinfer1::DataType::kINT32: elementSize = 4; break;
            case nvinfer1::DataType::kBOOL:  elementSize = 1; break;
            case nvinfer1::DataType::kUINT8: elementSize = 1; break;
            default: elementSize = 4; break;
            }

            info.bytes = elements * elementSize;
            info.devicePtr = nullptr;

            bindings_[name] = info;

            if (mode == nvinfer1::TensorIOMode::kINPUT) {
                inputNames_.push_back(name);
            }
            else {
                outputNames_.push_back(name);
            }
        }
    }

    // ============================================================================
    // 设置输入输出
    // ============================================================================

    Error TensorRTEngine::setInput(const std::string& name,
        void* devicePtr,
        size_t bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!context_) {
            return Error(ErrorCode::ModelLoadFailed, "Context not initialized");
        }

        auto it = bindings_.find(name);
        if (it == bindings_.end()) {
            return Error(ErrorCode::InvalidArgument,
                "Unknown input binding: " + name);
        }

        if (bytes < it->second.bytes) {
            return Error(ErrorCode::InvalidArgument,
                "Input buffer too small for: " + name);
        }

        it->second.devicePtr = devicePtr;

        if (!context_->setTensorAddress(name.c_str(), devicePtr)) {
            return Error(ErrorCode::InferenceFailed,
                "Failed to set tensor address: " + name);
        }

        return Error{};
    }

    Error TensorRTEngine::setOutput(const std::string& name,
        void* devicePtr,
        size_t bytes)
    {
        return setInput(name, devicePtr, bytes);
    }

    // ============================================================================
    // 推理
    // ============================================================================

    Error TensorRTEngine::infer(cudaStream_t stream) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!context_) {
            return Error(ErrorCode::ModelLoadFailed, "Context not initialized");
        }

        // 检查所有输入是否已设置
        for (const auto& name : inputNames_) {
            auto it = bindings_.find(name);
            if (it == bindings_.end() || it->second.devicePtr == nullptr) {
                return Error(ErrorCode::InferenceFailed,
                    "Input not set: " + name);
            }
        }

        // 计时
        cudaEventRecord(startEvent_, stream);

        // 执行推理
        bool ok = context_->enqueueV3(stream);

        cudaEventRecord(stopEvent_, stream);
        cudaEventSynchronize(stopEvent_);

        cudaEventElapsedTime(&lastInferenceMs_, startEvent_, stopEvent_);

        if (!ok) {
            return Error(ErrorCode::InferenceFailed,
                "TensorRT inference failed");
        }

        return Error{};
    }

    // ============================================================================
    // 查询
    // ============================================================================

    size_t TensorRTEngine::numBindings() const {
        return bindings_.size();
    }

    std::vector<std::string> TensorRTEngine::inputNames() const {
        return inputNames_;
    }

    std::vector<std::string> TensorRTEngine::outputNames() const {
        return outputNames_;
    }

    std::vector<int64_t> TensorRTEngine::inputShape(
        const std::string& name) const
    {
        std::vector<int64_t> result;

        auto it = bindings_.find(name);
        if (it == bindings_.end()) return result;

        for (int i = 0; i < it->second.dims.nbDims; ++i) {
            result.push_back(it->second.dims.d[i]);
        }

        return result;
    }

    std::vector<int64_t> TensorRTEngine::outputShape(
        const std::string& name) const
    {
        return inputShape(name);
    }

    size_t TensorRTEngine::bindingBytes(const std::string& name) const {
        auto it = bindings_.find(name);
        return (it != bindings_.end()) ? it->second.bytes : 0;
    }

} // namespace Lingjing
#endif