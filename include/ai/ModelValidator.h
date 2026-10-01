#pragma once

#include "core/Error.h"
#include "ai/IAIRepair.h"
#include "ai/ModelLoader.h"

#include <string>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 校验结果
    // ============================================================================

    struct ValidationResult {
        bool valid = false;

        // 检查项
        bool fileExists = false;
        bool formatSupported = false;
        bool sizeValid = false;
        bool inputShapeValid = false;
        bool outputShapeValid = false;
        bool precisionSupported = false;
        bool hashMatches = false;

        // 诊断信息
        std::string errorMessage;
        std::vector<std::string> warnings;
        std::vector<std::string> suggestions;

        // 元数据
        ModelMetadata metadata;
    };

    // ============================================================================
    // 模型校验器
    // ============================================================================

    class ModelValidator {
    public:
        // 完整校验
        static ValidationResult validate(
            const std::string& modelPath,
            const AIRepairConfig& expectedConfig);

        // 快速校验（仅检查文件存在性和格式）
        static Error quickValidate(const std::string& modelPath);

        // 校验 ONNX 模型结构
        static Error validateONNX(const std::string& path,
            ModelMetadata& outMetadata);

        // 校验 TensorRT 引擎
        static Error validateTensorRTEngine(const std::string& path,
            ModelMetadata& outMetadata);

        // 校验 OpenVINO IR
        static Error validateOpenVINOIR(const std::string& path,
            ModelMetadata& outMetadata);

        // 校验 SHA256
        static Error validateSHA256(const std::string& path,
            const std::string& expectedSHA256);

        // 校验输入形状
        static Error validateInputShape(const std::vector<int64_t>& shape,
            uint32_t expectedWidth,
            uint32_t expectedHeight);

    private:
        static bool isShapeDynamic(const std::vector<int64_t>& shape);
        static std::string shapeToString(const std::vector<int64_t>& shape);
    };

} // namespace Lingjing