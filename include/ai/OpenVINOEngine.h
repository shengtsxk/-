#pragma once
#ifdef LJ_INTEL

#include "core/Types.h"
#include "core/Error.h"
#include "ai/IAIRepair.h"

#include <openvino/openvino.hpp>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace Lingjing {

    // ============================================================================
    // OpenVINO 引擎
    // ============================================================================

    class OpenVINOEngine {
    public:
        OpenVINOEngine();
        ~OpenVINOEngine();

        // ====================================================================
        // 初始化
        // ====================================================================

        Error loadModel(const std::string& modelPath,
            const std::string& device = "GPU",
            bool enableFP16 = true);

        Error compileForDevice(const std::string& device);

        void destroy();

        // ====================================================================
        // 推理
        // ====================================================================

        // 设置输入（主机或设备内存）
        Error setInput(size_t index, const void* data, size_t bytes);

        // 执行推理
        Error infer();

        // 获取输出
        Error getOutput(size_t index, void* data, size_t bytes);

        // 异步推理
        Error inferAsync();
        Error waitAsync();

        // ====================================================================
        // 查询
        // ====================================================================

        bool isReady() const { return compiledModel_ != nullptr; }

        size_t numInputs() const;
        size_t numOutputs() const;

        std::vector<size_t> inputShape(size_t index) const;
        std::vector<size_t> outputShape(size_t index) const;

        std::string inputName(size_t index) const;
        std::string outputName(size_t index) const;

        float lastInferenceTimeMs() const { return lastInferenceMs_; }

    private:
        std::unique_ptr<ov::Core> core_;
        std::shared_ptr<ov::Model> model_;
        ov::CompiledModel compiledModel_;
        ov::InferRequest inferRequest_;

        std::vector<ov::Tensor> inputTensors_;
        std::vector<ov::Tensor> outputTensors_;

        std::chrono::steady_clock::time_point lastInferStart_;
        float lastInferenceMs_ = 0.0f;

        bool initialized_ = false;
        std::mutex mutex_;
    };

} // namespace Lingjing
#endif