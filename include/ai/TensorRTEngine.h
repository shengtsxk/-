#pragma once
#ifdef LJ_NVIDIA

#include "core/Types.h"
#include "core/Error.h"
#include "gpu/IGpuContext.h"
#include "ai/IAIRepair.h"
#include "ai/ModelLoader.h"

#include <NvInfer.h>
#include <cuda_runtime.h>

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>

namespace Lingjing {

    // ============================================================================
    // TensorRT Logger
    // ============================================================================

    class TRTLogger : public nvinfer1::ILogger {
    public:
        void log(Severity severity, const char* msg) noexcept override;
    };

    // ============================================================================
    // TensorRT 引擎
    // ============================================================================

    class TensorRTEngine {
    public:
        TensorRTEngine();
        ~TensorRTEngine();

        // ====================================================================
        // 初始化
        // ====================================================================

        // 从 ONNX 构建引擎
        Error buildFromONNX(const std::string& onnxPath,
            const AIRepairConfig& config);

        // 从序列化引擎加载
        Error loadFromEngineFile(const std::string& enginePath);

        // 保存引擎到文件
        Error saveEngine(const std::string& enginePath);

        // 销毁
        void destroy();

        // ====================================================================
        // 推理
        // ====================================================================

        // 设置输入（设备内存指针）
        Error setInput(const std::string& name, void* devicePtr, size_t bytes);

        // 设置输出
        Error setOutput(const std::string& name, void* devicePtr, size_t bytes);

        // 执行推理
        Error infer(cudaStream_t stream);

        // ====================================================================
        // 查询
        // ====================================================================

        bool isReady() const { return engine_ != nullptr; }

        size_t numBindings() const;

        std::vector<std::string> inputNames() const;
        std::vector<std::string> outputNames() const;

        std::vector<int64_t> inputShape(const std::string& name) const;
        std::vector<int64_t> outputShape(const std::string& name) const;

        size_t bindingBytes(const std::string& name) const;

        // ====================================================================
        // 性能
        // ====================================================================

        float lastInferenceTimeMs() const { return lastInferenceMs_; }

    private:
        // TensorRT 对象
        std::unique_ptr<TRTLogger> logger_;
        std::unique_ptr<nvinfer1::IRuntime> runtime_;
        std::unique_ptr<nvinfer1::ICudaEngine> engine_;
        std::unique_ptr<nvinfer1::IExecutionContext> context_;

        // 绑定信息
        struct BindingInfo {
            std::string name;
            nvinfer1::TensorIOMode mode;
            nvinfer1::DataType dataType;
            nvinfer1::Dims dims;
            size_t bytes = 0;
            void* devicePtr = nullptr;
        };

        std::unordered_map<std::string, BindingInfo> bindings_;
        std::vector<std::string> inputNames_;
        std::vector<std::string> outputNames_;

        // 计时
        cudaEvent_t startEvent_ = nullptr;
        cudaEvent_t stopEvent_ = nullptr;
        float lastInferenceMs_ = 0.0f;

        // 配置
        AIRepairConfig config_;
        bool initialized_ = false;

        std::mutex mutex_;

        // 辅助
        void clearBindings();
        void setupBindings();
    };

} // namespace Lingjing
#endif