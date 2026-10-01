#pragma once

#include "core/Types.h"
#include "core/Error.h"
#include "ai/IAIRepair.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace Lingjing {

    // ============================================================================
    // 模型格式
    // ============================================================================

    enum class ModelFormat : uint32_t {
        Unknown = 0,
        ONNX,
        TensorRT_Engine,
        OpenVINO_IR,
        TorchScript,
        Safetensors,
    };

    inline const char* modelFormatName(ModelFormat fmt) {
        switch (fmt) {
        case ModelFormat::ONNX:              return "ONNX";
        case ModelFormat::TensorRT_Engine:   return "TensorRT Engine";
        case ModelFormat::OpenVINO_IR:       return "OpenVINO IR";
        case ModelFormat::TorchScript:       return "TorchScript";
        case ModelFormat::Safetensors:       return "Safetensors";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // 模型信息
    // ============================================================================

    struct ModelFileInfo {
        std::string path;
        std::string fileName;
        ModelFormat format = ModelFormat::Unknown;
        size_t fileSize = 0;
        int64_t modifiedTimestamp = 0;
        std::string sha256;
        bool valid = false;
    };

    // ============================================================================
    // 模型加载器
    // ============================================================================

    class ModelLoader {
    public:
        // 从路径加载模型，自动识别格式
        static Error loadModel(const std::string& path,
            std::vector<uint8_t>& outData,
            ModelFileInfo& outInfo);

        // 获取模型文件信息（不加载内容）
        static ModelFileInfo getModelFileInfo(const std::string& path);

        // 检测模型格式
        static ModelFormat detectFormat(const std::string& path);

        // 校验模型文件（是否存在、格式正确）
        static Error validateModelFile(const std::string& path);

        // 计算文件 SHA256
        static std::string computeSHA256(const std::string& path);

        // 从目录中扫描所有模型
        static std::vector<ModelFileInfo> scanModels(const std::string& directory);

        // 获取模型默认目录
        static std::string getDefaultModelDirectory();

        // 获取用户模型目录
        static std::string getUserModelDirectory();

        // 获取模型缓存目录
        static std::string getModelCacheDirectory();

        // 保存模型到用户目录
        static Error saveModelToUserDir(const std::string& sourcePath,
            const std::string& targetName);
    };

    // ============================================================================
    // ONNX 模型解析（简化版）
    // ============================================================================

    class OnnxModelParser {
    public:
        struct TensorInfo {
            std::string name;
            std::string dtype;
            std::vector<int64_t> shape;
        };

        struct OnnxModel {
            std::string irVersion;
            std::string producerName;
            std::string producerVersion;
            std::string domain;
            int64_t modelVersion = 0;
            std::string docString;

            std::vector<TensorInfo> inputs;
            std::vector<TensorInfo> outputs;

            std::vector<std::string> opsets;

            // 元数据
            std::map<std::string, std::string> metadataProps;
        };

        static Error parse(const std::vector<uint8_t>& data,
            OnnxModel& outModel);

        static Error parseFromFile(const std::string& path,
            OnnxModel& outModel);
    };

    // ============================================================================
    // 模型注册表（用于扫描和列出用户可用模型）
    // ============================================================================

    class ModelRegistry {
    public:
        struct Entry {
            std::string name;
            std::string displayName;
            std::string description;
            std::string path;
            ModelFormat format = ModelFormat::Unknown;
            ModelPrecision precision = ModelPrecision::FP16;
            bool recommended = false;
            bool available = false;
            size_t fileSize = 0;
        };

        ModelRegistry();

        // 扫描所有模型目录
        void scanAll();

        // 获取所有模型
        const std::vector<Entry>& entries() const { return entries_; }

        // 按名称查找
        const Entry* findByName(const std::string& name) const;

        // 推荐模型
        const Entry* recommendedModel() const;

        // 清除
        void clear() { entries_.clear(); }

    private:
        void scanDirectory(const std::string& dir);
        void registerBuiltinModels();
        void probeFile(Entry& entry);

        std::vector<Entry> entries_;
    };

} // namespace Lingjing