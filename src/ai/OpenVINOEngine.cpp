#ifdef LJ_INTEL

#include "ai/OpenVINOEngine.h"
#include "core/Logger.h"

#include <openvino/core/preprocess/pre_post_process.hpp>
#include <openvino/runtime/intel_gpu/ocl/ocl.hpp>
#include <chrono>

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    OpenVINOEngine::OpenVINOEngine() = default;

    OpenVINOEngine::~OpenVINOEngine() {
        destroy();
    }

    // ============================================================================
    // 销毁
    // ============================================================================

    void OpenVINOEngine::destroy() {
        std::lock_guard<std::mutex> lock(mutex_);

        inputTensors_.clear();
        outputTensors_.clear();

        inferRequest_ = ov::InferRequest();
        compiledModel_ = ov::CompiledModel();
        model_.reset();
        core_.reset();

        initialized_ = false;
    }

    // ============================================================================
    // 加载模型
    // ============================================================================

    Error OpenVINOEngine::loadModel(const std::string& modelPath,
        const std::string& device,
        bool enableFP16)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        try {
            core_ = std::make_unique<ov::Core>();

            // 列出可用设备
            auto devices = core_->get_available_devices();

            LOG_INFO("OpenVINO available devices:");
            for (const auto& d : devices) {
                LOG_INFO("  - %s", d.c_str());
            }

            // 读取模型
            model_ = core_->read_model(modelPath);

            if (!model_) {
                return Error(ErrorCode::ModelLoadFailed,
                    "Failed to read model: " + modelPath);
            }

            // 配置
            ov::AnyMap config;

            if (enableFP16) {
                config[ov::hint::inference_precision.name()] = ov::element::f16;
            }

            // 性能模式
            config[ov::hint::performance_mode.name()] =
                ov::hint::PerformanceMode::LATENCY;

            // 编译模型
            compiledModel_ = core_->compile_model(model_, device, config);

            if (!compiledModel_) {
                return Error(ErrorCode::ModelLoadFailed,
                    "Failed to compile model for device: " + device);
            }

            // 创建推理请求
            inferRequest_ = compiledModel_.create_infer_request();

            // 获取输入输出信息
            auto inputs = compiledModel_.inputs();
            auto outputs = compiledModel_.outputs();

            inputTensors_.resize(inputs.size());
            outputTensors_.resize(outputs.size());

            LOG_INFO("OpenVINO model loaded: %zu inputs, %zu outputs",
                inputs.size(), outputs.size());

            initialized_ = true;

            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::ModelLoadFailed,
                std::string("OpenVINO error: ") + e.what());
        }
        catch (const std::exception& e) {
            return Error(ErrorCode::SystemError,
                std::string("Failed to load model: ") + e.what());
        }
    }

    Error OpenVINOEngine::compileForDevice(const std::string& device) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!model_) {
            return Error(ErrorCode::ModelLoadFailed, "Model not loaded");
        }

        try {
            compiledModel_ = core_->compile_model(model_, device);
            inferRequest_ = compiledModel_.create_infer_request();

            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::ModelLoadFailed,
                std::string("Compile failed: ") + e.what());
        }
    }

    // ============================================================================
    // 推理
    // ============================================================================

    Error OpenVINOEngine::setInput(size_t index,
        const void* data,
        size_t bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_) {
            return Error(ErrorCode::ModelLoadFailed, "Not initialized");
        }

        try {
            auto inputs = compiledModel_.inputs();

            if (index >= inputs.size()) {
                return Error(ErrorCode::InvalidArgument, "Invalid input index");
            }

            auto input = inputs[index];
            auto shape = input.get_shape();
            auto elementType = input.get_element_type();

            // 创建或复用 tensor
            if (inputTensors_[index].get_size() == 0 ||
                inputTensors_[index].get_shape() != shape)
            {
                inputTensors_[index] = ov::Tensor(elementType, shape);
            }

            // 拷贝数据
            size_t expectedBytes = inputTensors_[index].get_byte_size();

            if (bytes < expectedBytes) {
                return Error(ErrorCode::InvalidArgument,
                    "Buffer too small");
            }

            std::memcpy(inputTensors_[index].data(), data, expectedBytes);

            // 设置到推理请求
            inferRequest_.set_tensor(input, inputTensors_[index]);

            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::InferenceFailed,
                std::string("Set input failed: ") + e.what());
        }
    }

    Error OpenVINOEngine::infer() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_) {
            return Error(ErrorCode::ModelLoadFailed, "Not initialized");
        }

        try {
            auto start = std::chrono::steady_clock::now();

            inferRequest_.infer();

            auto end = std::chrono::steady_clock::now();
            lastInferenceMs_ = std::chrono::duration<float, std::milli>(
                end - start).count();

            // 获取输出
            auto outputs = compiledModel_.outputs();

            for (size_t i = 0; i < outputs.size(); ++i) {
                outputTensors_[i] = inferRequest_.get_tensor(outputs[i]);
            }

            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::InferenceFailed,
                std::string("Inference failed: ") + e.what());
        }
    }

    Error OpenVINOEngine::getOutput(size_t index, void* data, size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_) {
            return Error(ErrorCode::ModelLoadFailed, "Not initialized");
        }

        try {
            auto outputs = compiledModel_.outputs();

            if (index >= outputs.size()) {
                return Error(ErrorCode::InvalidArgument, "Invalid output index");
            }

            auto outputTensor = inferRequest_.get_tensor(outputs[index]);
            size_t expectedBytes = outputTensor.get_byte_size();

            if (bytes < expectedBytes) {
                return Error(ErrorCode::InvalidArgument, "Buffer too small");
            }

            std::memcpy(data, outputTensor.data(), expectedBytes);

            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::InferenceFailed,
                std::string("Get output failed: ") + e.what());
        }
    }

    Error OpenVINOEngine::inferAsync() {
        if (!initialized_) {
            return Error(ErrorCode::ModelLoadFailed, "Not initialized");
        }

        try {
            inferRequest_.start_async();
            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::InferenceFailed,
                std::string("Async inference failed: ") + e.what());
        }
    }

    Error OpenVINOEngine::waitAsync() {
        if (!initialized_) {
            return Error(ErrorCode::ModelLoadFailed, "Not initialized");
        }

        try {
            inferRequest_.wait();

            auto outputs = compiledModel_.outputs();

            for (size_t i = 0; i < outputs.size(); ++i) {
                outputTensors_[i] = inferRequest_.get_tensor(outputs[i]);
            }

            return Error{};
        }
        catch (const ov::Exception& e) {
            return Error(ErrorCode::InferenceFailed,
                std::string("Wait failed: ") + e.what());
        }
    }

    // ============================================================================
    // 查询
    // ============================================================================

    size_t OpenVINOEngine::numInputs() const {
        if (!compiledModel_) return 0;
        return compiledModel_.inputs().size();
    }

    size_t OpenVINOEngine::numOutputs() const {
        if (!compiledModel_) return 0;
        return compiledModel_.outputs().size();
    }

    std::vector<size_t> OpenVINOEngine::inputShape(size_t index) const {
        std::vector<size_t> result;

        if (!compiledModel_) return result;

        auto inputs = compiledModel_.inputs();
        if (index >= inputs.size()) return result;

        auto shape = inputs[index].get_shape();

        for (auto d : shape) {
            result.push_back(d);
        }

        return result;
    }

    std::vector<size_t> OpenVINOEngine::outputShape(size_t index) const {
        std::vector<size_t> result;

        if (!compiledModel_) return result;

        auto outputs = compiledModel_.outputs();
        if (index >= outputs.size()) return result;

        auto shape = outputs[index].get_shape();

        for (auto d : shape) {
            result.push_back(d);
        }

        return result;
    }

    std::string OpenVINOEngine::inputName(size_t index) const {
        if (!compiledModel_) return "";

        auto inputs = compiledModel_.inputs();
        if (index >= inputs.size()) return "";

        return inputs[index].get_any_name();
    }

    std::string OpenVINOEngine::outputName(size_t index) const {
        if (!compiledModel_) return "";

        auto outputs = compiledModel_.outputs();
        if (index >= outputs.size()) return "";

        return outputs[index].get_any_name();
    }

} // namespace Lingjing
#endif