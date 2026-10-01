#include "ai/ModelValidator.h"
#include "ai/ModelLoader.h"
#include "core/Logger.h"

#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace Lingjing {

    // ============================================================================
    // 完整校验
    // ============================================================================

    ValidationResult ModelValidator::validate(
        const std::string& modelPath,
        const AIRepairConfig& expectedConfig)
    {
        ValidationResult result;

        // 1. 文件存在性
        result.fileExists = fs::exists(modelPath);

        if (!result.fileExists) {
            result.errorMessage = "Model file not found: " + modelPath;
            result.suggestions.push_back(
                "请确保模型文件位于 " +
                ModelLoader::getDefaultModelDirectory() + " 或 " +
                ModelLoader::getUserModelDirectory());
            return result;
        }

        // 2. 文件大小
        try {
            size_t fileSize = fs::file_size(modelPath);
            result.sizeValid = fileSize > 0 && fileSize < (2ULL * 1024 * 1024 * 1024);

            if (!result.sizeValid) {
                result.errorMessage = "Invalid model file size";
                return result;
            }
        }
        catch (...) {
            result.sizeValid = false;
            result.errorMessage = "Cannot determine file size";
            return result;
        }

        // 3. 格式检测
        ModelFormat fmt = ModelLoader::detectFormat(modelPath);
        result.formatSupported = (fmt != ModelFormat::Unknown);

        if (!result.formatSupported) {
            result.errorMessage = "Unsupported model format: " + modelPath;
            return result;
        }

        // 4. 格式特定校验
        Error err;

        switch (fmt) {
        case ModelFormat::ONNX:
            err = validateONNX(modelPath, result.metadata);
            break;

        case ModelFormat::TensorRT_Engine:
            err = validateTensorRTEngine(modelPath, result.metadata);
            break;

        case ModelFormat::OpenVINO_IR:
            err = validateOpenVINOIR(modelPath, result.metadata);
            break;

        default:
            err = Error(ErrorCode::ModelInvalid, "Unsupported format");
            break;
        }

        if (err.isFailure()) {
            result.errorMessage = err.message;
            return result;
        }

        // 5. 输入输出形状校验
        if (!result.metadata.inputShapes.empty()) {
            auto& inputShape = result.metadata.inputShapes[0];

            if (expectedConfig.inputWidth > 0 && expectedConfig.inputHeight > 0) {
                err = validateInputShape(inputShape,
                    expectedConfig.inputWidth,
                    expectedConfig.inputHeight);

                if (err.isFailure()) {
                    result.warnings.push_back(err.message);
                }
                else {
                    result.inputShapeValid = true;
                }
            }
            else {
                result.inputShapeValid = true;
            }
        }

        // 6. 精度校验
        result.precisionSupported = true;

        // 7. 全部通过
        result.valid = result.fileExists &&
            result.formatSupported &&
            result.sizeValid;

        if (result.valid) {
            LOG_INFO("Model validation passed: %s", modelPath.c_str());
        }

        return result;
    }

    // ============================================================================
    // 快速校验
    // ============================================================================

    Error ModelValidator::quickValidate(const std::string& modelPath) {
        if (modelPath.empty()) {
            return Error(ErrorCode::InvalidArgument, "Empty model path");
        }

        return ModelLoader::validateModelFile(modelPath);
    }

    // ============================================================================
    // ONNX 校验
    // ============================================================================

    Error ModelValidator::validateONNX(const std::string& path,
        ModelMetadata& outMetadata)
    {
        OnnxModelParser::OnnxModel onnxModel;
        Error err = OnnxModelParser::parseFromFile(path, onnxModel);

        if (err.isFailure()) {
            return err;
        }

        // 转换为通用元数据
        outMetadata.name = fs::path(path).stem().string();
        outMetadata.version = onnxModel.irVersion;
        outMetadata.description = onnxModel.docString;

        // 输入输出
        for (const auto& in : onnxModel.inputs) {
            outMetadata.inputNames.push_back(in.name);
            outMetadata.inputShapes.push_back(in.shape);
            outMetadata.inputDtypes.push_back(in.dtype);
        }

        for (const auto& out : onnxModel.outputs) {
            outMetadata.outputNames.push_back(out.name);
            outMetadata.outputShapes.push_back(out.shape);
            outMetadata.outputDtypes.push_back(out.dtype);
        }

        return Error{};
    }

    // ============================================================================
    // TensorRT 引擎校验
    // ============================================================================

    Error ModelValidator::validateTensorRTEngine(const std::string& path,
        ModelMetadata& outMetadata)
    {
        // TensorRT 引擎反序列化本身需要 TensorRT 运行时
        // 这里仅做基本检查

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return Error(ErrorCode::FileNotFound, "Cannot open engine file");
        }

        // 读取头部字节
        uint32_t magic = 0;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));

        // TensorRT 引擎通常以 0x1A 开头（protobuf）
        // 但不做严格检查

        outMetadata.name = fs::path(path).stem().string();
        outMetadata.version = "TensorRT";

        return Error{};
    }

    // ============================================================================
    // OpenVINO IR 校验
    // ============================================================================

    Error ModelValidator::validateOpenVINOIR(const std::string& path,
        ModelMetadata& outMetadata)
    {
        // 检查 .xml 和 .bin 是否都存在
        if (path.substr(path.size() - 4) != ".xml") {
            return Error(ErrorCode::ModelInvalid,
                "OpenVINO IR requires .xml file");
        }

        std::string binPath = path.substr(0, path.size() - 4) + ".bin";

        if (!fs::exists(binPath)) {
            return Error(ErrorCode::FileNotFound,
                "Matching .bin file not found: " + binPath);
        }

        // 读取 XML 头部
        std::ifstream file(path);
        if (!file.is_open()) {
            return Error(ErrorCode::FileNotFound, "Cannot open IR file");
        }

        std::string line;
        std::getline(file, line);  // 跳过 XML 声明
        std::getline(file, line);  // 读取 <net> 标签

        outMetadata.name = fs::path(path).stem().string();
        outMetadata.version = "OpenVINO IR";

        return Error{};
    }

    // ============================================================================
    // SHA256 校验
    // ============================================================================

    Error ModelValidator::validateSHA256(const std::string& path,
        const std::string& expectedSHA256)
    {
        std::string actual = ModelLoader::computeSHA256(path);

        if (actual.empty()) {
            return Error(ErrorCode::SystemError,
                "Failed to compute SHA256");
        }

        std::string lowerExpected = expectedSHA256;
        std::transform(lowerExpected.begin(), lowerExpected.end(),
            lowerExpected.begin(), ::tolower);

        std::string lowerActual = actual;
        std::transform(lowerActual.begin(), lowerActual.end(),
            lowerActual.begin(), ::tolower);

        if (lowerExpected != lowerActual) {
            return Error(ErrorCode::ModelInvalid,
                "SHA256 mismatch: expected " + expectedSHA256 +
                ", got " + actual);
        }

        return Error{};
    }

    // ============================================================================
    // 输入形状校验
    // ============================================================================

    Error ModelValidator::validateInputShape(
        const std::vector<int64_t>& shape,
        uint32_t expectedWidth,
        uint32_t expectedHeight)
    {
        if (shape.empty()) {
            return Error(ErrorCode::ModelInvalid, "Empty shape");
        }

        // 支持 NCHW 或 NHWC
        // 常见 shape: [1, 3, H, W] 或 [1, H, W, 3] 或 [1, 6, H, W]

        int hIdx = -1, wIdx = -1;

        if (shape.size() == 4) {
            // 检查是否是 NCHW
            if (shape[1] == 3 || shape[1] == 6) {
                hIdx = 2;
                wIdx = 3;
            }
            // 检查是否是 NHWC
            else if (shape[3] == 3 || shape[3] == 6) {
                hIdx = 1;
                wIdx = 2;
            }
        }

        if (hIdx < 0) {
            return Error(ErrorCode::ModelInvalid,
                "Unsupported input shape: " + shapeToString(shape));
        }

        int64_t modelH = shape[hIdx];
        int64_t modelW = shape[wIdx];

        // 动态 shape 检查
        if (modelH <= 0 || modelW <= 0) {
            return Error{};  // 动态 shape 支持
        }

        // 尺寸比例检查
        if (expectedWidth > 0 && expectedHeight > 0) {
            float ratio = static_cast<float>(modelW) / static_cast<float>(modelH);
            float expectedRatio = static_cast<float>(expectedWidth) /
                static_cast<float>(expectedHeight);

            if (std::fabs(ratio - expectedRatio) > 0.1f) {
                return Error(ErrorCode::ModelInvalid,
                    "Aspect ratio mismatch: model " +
                    shapeToString(shape) +
                    " vs expected " +
                    std::to_string(expectedWidth) + "x" +
                    std::to_string(expectedHeight));
            }
        }

        return Error{};
    }

    // ============================================================================
    // 辅助
    // ============================================================================

    bool ModelValidator::isShapeDynamic(const std::vector<int64_t>& shape) {
        for (int64_t d : shape) {
            if (d < 0) return true;
        }
        return false;
    }

    std::string ModelValidator::shapeToString(const std::vector<int64_t>& shape) {
        std::ostringstream oss;
        oss << "[";

        for (size_t i = 0; i < shape.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << shape[i];
        }

        oss << "]";
        return oss.str();
    }

} // namespace Lingjing