#pragma once

#include "core/Types.h"
#include "core/DeviceCaps.h"
#include "core/Error.h"
#include "gpu/IGpuContext.h"

#include <memory>
#include <string>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // AI 修复后端类型
    // ============================================================================

    enum class AIRepairBackend : uint32_t {
        None = 0,
        TensorRT,
        OpenVINO,
        DirectML,
    };

    inline const char* aiRepairBackendName(AIRepairBackend backend) {
        switch (backend) {
        case AIRepairBackend::TensorRT:  return "TensorRT";
        case AIRepairBackend::OpenVINO:  return "OpenVINO";
        case AIRepairBackend::DirectML:  return "DirectML";
        default: return "None";
        }
    }

    // ============================================================================
    // 模型精度
    // ============================================================================

    enum class ModelPrecision : uint32_t {
        FP32 = 0,
        FP16,
        INT8,
        BF16,
    };

    inline const char* modelPrecisionName(ModelPrecision p) {
        switch (p) {
        case ModelPrecision::FP32: return "FP32";
        case ModelPrecision::FP16: return "FP16";
        case ModelPrecision::INT8: return "INT8";
        case ModelPrecision::BF16: return "BF16";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // AI 修复配置
    // ============================================================================

    struct AIRepairConfig {
        // 模型路径
        std::string modelPath;

        // 推理后端
        AIRepairBackend backend = AIRepairBackend::None;

        // 精度
        ModelPrecision precision = ModelPrecision::FP16;

        // 输入输出尺寸
        uint32_t inputWidth = 0;
        uint32_t inputHeight = 0;
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;

        // 批处理
        uint32_t maxBatchSize = 1;

        // 缓存
        bool enableEngineCache = true;
        std::string cacheDir;

        // 精度调优
        bool enableFp16 = true;
        bool enableInt8 = false;
        bool enableBF16 = false;

        // 性能
        size_t workspaceSizeBytes = 512ULL * 1024 * 1024;   // 512 MB
        bool enableCudaGraph = true;
        bool enablePinnedMemory = true;

        // 动态 shape
        bool enableDynamicShape = false;
        uint32_t minBatchSize = 1;
        uint32_t optBatchSize = 1;
        uint32_t maxBatch = 4;

        // 模型特定参数
        struct ModelParams {
            // 输入均值/标准差（用于归一化）
            float meanR = 0.485f;
            float meanG = 0.456f;
            float meanB = 0.406f;
            float stdR = 0.229f;
            float stdG = 0.224f;
            float stdB = 0.225f;

            // 输出缩放
            float outputScale = 1.0f;
            float outputBias = 0.0f;
        } modelParams;
    };

    // ============================================================================
    // 模型元数据
    // ============================================================================

    struct ModelMetadata {
        // 基本信息
        std::string name;
        std::string version;
        std::string description;
        std::string author;

        // 输入
        std::vector<std::string> inputNames;
        std::vector<std::vector<int64_t>> inputShapes;
        std::vector<std::string> inputDtypes;

        // 输出
        std::vector<std::string> outputNames;
        std::vector<std::vector<int64_t>> outputShapes;
        std::vector<std::string> outputDtypes;

        // 校验
        std::string sha256;
        size_t fileSize = 0;
        int64_t createdTimestamp = 0;

        // 训练
        std::string trainingDataset;
        float reportedPSNR = 0.0f;
        float reportedSSIM = 0.0f;
    };

    // ============================================================================
    // AI 修复结果
    // ============================================================================

    struct AIRepairResult {
        GpuTexture output;             // 修复后的帧
        bool success = false;

        // 性能
        float inferenceTimeMs = 0.0f;
        float preprocessTimeMs = 0.0f;
        float postprocessTimeMs = 0.0f;

        // 质量
        float estimatedPSNR = 0.0f;
        float estimatedSSIM = 0.0f;

        void reset() {
            success = false;
            inferenceTimeMs = 0.0f;
            preprocessTimeMs = 0.0f;
            postprocessTimeMs = 0.0f;
            estimatedPSNR = 0.0f;
            estimatedSSIM = 0.0f;
        }
    };

    // ============================================================================
    // AI 修复接口
    // ============================================================================

    class IAIRepair {
    public:
        virtual ~IAIRepair() = default;

        // ====================================================================
        // 生命周期
        // ====================================================================

        virtual bool initialize(IGpuContext* gpuContext,
            const AIRepairConfig& config) = 0;

        virtual void shutdown() = 0;

        // ====================================================================
        // 推理
        // ====================================================================

        // 修复单个帧
        virtual bool repair(const GpuTexture& input,
            AIRepairResult& result) = 0;

        // 修复多个帧（批处理）
        virtual bool repairBatch(const std::vector<GpuTexture>& inputs,
            std::vector<AIRepairResult>& results) = 0;

        // ====================================================================
        // 查询
        // ====================================================================

        virtual bool isReady() const = 0;
        virtual AIRepairBackend backend() const = 0;
        virtual const char* engineName() const = 0;

        virtual const ModelMetadata& metadata() const = 0;

        // ====================================================================
        // 性能
        // ====================================================================

        virtual float averageInferenceTimeMs() const = 0;
        virtual size_t memoryUsageBytes() const = 0;

        // ====================================================================
        // 模型热切换
        // ====================================================================

        virtual bool reloadModel(const std::string& newModelPath) = 0;
    };

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IAIRepair> createTensorRTAIRepair();
    std::unique_ptr<IAIRepair> createOpenVINOAIREpair();

} // namespace Lingjing