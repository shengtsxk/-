#include "ai/ModelLoader.h"
#include "core/Logger.h"
#include "core/MathUtils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <map>
#include <windows.h>
#include <bcrypt.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace Lingjing {

    // ============================================================================
    // 模型格式检测
    // ============================================================================

    ModelFormat ModelLoader::detectFormat(const std::string& path) {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".onnx") {
            return ModelFormat::ONNX;
        }
        else if (ext == ".trt" || ext == ".engine" || ext == ".plan") {
            return ModelFormat::TensorRT_Engine;
        }
        else if (ext == ".xml") {
            // OpenVINO 检查对应 .bin 文件
            std::string binPath = path.substr(0, path.size() - 4) + ".bin";
            if (fs::exists(binPath)) {
                return ModelFormat::OpenVINO_IR;
            }
            return ModelFormat::Unknown;
        }
        else if (ext == ".pt" || ext == ".torchscript") {
            return ModelFormat::TorchScript;
        }
        else if (ext == ".safetensors") {
            return ModelFormat::Safetensors;
        }

        return ModelFormat::Unknown;
    }

    // ============================================================================
    // SHA256 计算
    // ============================================================================

    std::string ModelLoader::computeSHA256(const std::string& path) {
        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return "";

        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        std::string result;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
            if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0) {
                unsigned char buffer[8192];
                DWORD bytesRead = 0;
                while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
                    BCryptHashData(hHash, buffer, bytesRead, 0);
                }
                unsigned char hash[32];
                if (BCryptFinishHash(hHash, hash, sizeof(hash), 0) == 0) {
                    char hex[65];
                    for (int i = 0; i < 32; ++i) sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
                    hex[64] = 0;
                    result = hex;
                }
                BCryptDestroyHash(hHash);
            }
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
        CloseHandle(hFile);
        return result;
    }

    // ============================================================================
    // 获取文件信息
    // ============================================================================

    ModelFileInfo ModelLoader::getModelFileInfo(const std::string& path) {
        ModelFileInfo info;
        info.path = path;

        try {
            if (!fs::exists(path) || !fs::is_regular_file(path)) {
                LOG_WARN("Model file not found: %s", path.c_str());
                return info;
            }

            info.fileName = fs::path(path).filename().string();
            info.fileSize = fs::file_size(path);
            info.format = detectFormat(path);

            auto ftime = fs::last_write_time(path);
            info.modifiedTimestamp = std::chrono::duration_cast<
                std::chrono::seconds>(ftime.time_since_epoch()).count();

            info.valid = (info.format != ModelFormat::Unknown);
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to get file info for %s: %s", path.c_str(), e.what());
        }

        return info;
    }

    // ============================================================================
    // 加载模型
    // ============================================================================

    Error ModelLoader::loadModel(const std::string& path,
        std::vector<uint8_t>& outData,
        ModelFileInfo& outInfo)
    {
        outInfo = getModelFileInfo(path);

        if (!outInfo.valid) {
            return Error(ErrorCode::FileNotFound,
                "Model file not found or unsupported format: " + path);
        }

        try {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return Error(ErrorCode::FileNotFound,
                    "Failed to open model file: " + path);
            }

            outData.resize(outInfo.fileSize);

            file.read(reinterpret_cast<char*>(outData.data()),
                static_cast<std::streamsize>(outInfo.fileSize));

            if (!file) {
                return Error(ErrorCode::SystemError,
                    "Failed to read model file: " + path);
            }

            LOG_INFO("Model loaded: %s (%.2f MB, %s)",
                path.c_str(),
                static_cast<double>(outInfo.fileSize) / (1024.0 * 1024.0),
                modelFormatName(outInfo.format));

            return Error{};
        }
        catch (const std::exception& e) {
            return Error(ErrorCode::SystemError,
                std::string("Failed to load model: ") + e.what());
        }
    }

    // ============================================================================
    // 校验模型文件
    // ============================================================================

    Error ModelLoader::validateModelFile(const std::string& path) {
        if (path.empty()) {
            return Error(ErrorCode::InvalidArgument, "Empty model path");
        }

        if (!fs::exists(path)) {
            return Error(ErrorCode::FileNotFound,
                "Model file does not exist: " + path);
        }

        ModelFormat fmt = detectFormat(path);

        if (fmt == ModelFormat::Unknown) {
            return Error(ErrorCode::ModelInvalid,
                "Unsupported model format: " + path);
        }

        ModelFileInfo info = getModelFileInfo(path);

        if (info.fileSize == 0) {
            return Error(ErrorCode::ModelInvalid,
                "Empty model file: " + path);
        }

        // 最小大小检查
        if (fmt == ModelFormat::ONNX && info.fileSize < 100) {
            return Error(ErrorCode::ModelInvalid,
                "Model file too small to be valid ONNX");
        }

        if (fmt == ModelFormat::TensorRT_Engine && info.fileSize < 100) {
            return Error(ErrorCode::ModelInvalid,
                "Model file too small to be valid TensorRT engine");
        }

        return Error{};
    }

    // ============================================================================
    // 扫描目录
    // ============================================================================

    std::vector<ModelFileInfo> ModelLoader::scanModels(
        const std::string& directory)
    {
        std::vector<ModelFileInfo> results;

        try {
            if (!fs::exists(directory) || !fs::is_directory(directory)) {
                return results;
            }

            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".onnx" || ext == ".trt" || ext == ".engine" ||
                    ext == ".xml" || ext == ".pt" || ext == ".safetensors") {

                    auto info = getModelFileInfo(entry.path().string());

                    if (info.valid) {
                        results.push_back(std::move(info));
                    }
                }
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to scan models directory: %s", e.what());
        }

        return results;
    }

    // ============================================================================
    // 目录获取
    // ============================================================================

    std::string ModelLoader::getDefaultModelDirectory() {
        // 应用程序同级目录下的 models/
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);

        fs::path p(exePath);
        return (p.parent_path() / "models").string();
    }

    std::string ModelLoader::getUserModelDirectory() {
#ifdef _WIN32
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA,
            nullptr, 0, path))) {
            return (fs::path(path) / "Lingjing" / "models").string();
        }
#endif

        return (fs::current_path() / "user_models").string();
    }

    std::string ModelLoader::getModelCacheDirectory() {
#ifdef _WIN32
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA,
            nullptr, 0, path))) {
            return (fs::path(path) / "Lingjing" / "cache").string();
        }
#endif

        return (fs::current_path() / "cache").string();
    }

    Error ModelLoader::saveModelToUserDir(const std::string& sourcePath,
        const std::string& targetName)
    {
        try {
            std::string userDir = getUserModelDirectory();
            fs::create_directories(userDir);

            fs::path targetPath = fs::path(userDir) / targetName;

            if (fs::exists(targetPath)) {
                return Error(ErrorCode::InvalidArgument,
                    "Target model already exists: " +
                    targetPath.string());
            }

            fs::copy_file(sourcePath, targetPath,
                fs::copy_options::overwrite_existing);

            LOG_INFO("Model saved to user dir: %s", targetPath.string().c_str());

            return Error{};
        }
        catch (const std::exception& e) {
            return Error(ErrorCode::SystemError,
                std::string("Failed to save model: ") + e.what());
        }
    }

    // ============================================================================
    // ONNX 解析（简化实现）
    // ============================================================================

    Error OnnxModelParser::parse(const std::vector<uint8_t>& data,
        OnnxModel& outModel)
    {
        // 简化的 ONNX 解析，只读取关键信息
        // 完整的 ONNX Protobuf 解析需要 protobuf 库

        if (data.size() < 16) {
            return Error(ErrorCode::ModelInvalid, "ONNX data too small");
        }

        // 检查 ONNX 魔数
        // ONNX 文件以 \x08 开头（protobuf field 1 varint）

        try {
            // 使用简化解析，仅提取输入输出形状
            // 实际生产环境应使用 ONNX Runtime 或 protobuf 库

            size_t offset = 0;

            while (offset < data.size()) {
                uint8_t fieldByte = data[offset];
                uint32_t fieldNum = fieldByte >> 3;
                uint32_t wireType = fieldByte & 0x07;

                // IR version
                if (fieldNum == 1 && wireType == 0) {
                    // varint
                    uint64_t value = 0;
                    int shift = 0;
                    ++offset;

                    while (offset < data.size()) {
                        uint8_t b = data[offset++];
                        value |= (static_cast<uint64_t>(b & 0x7F) << shift);
                        if ((b & 0x80) == 0) break;
                        shift += 7;
                    }

                    outModel.irVersion = std::to_string(value);
                }
                // 跳过其他字段
                else if (wireType == 2) {
                    // length-delimited
                    ++offset;
                    uint64_t length = 0;
                    int shift = 0;

                    while (offset < data.size()) {
                        uint8_t b = data[offset++];
                        length |= (static_cast<uint64_t>(b & 0x7F) << shift);
                        if ((b & 0x80) == 0) break;
                        shift += 7;
                    }

                    offset += length;
                }
                else {
                    // 其他类型，简单跳过
                    ++offset;
                }
            }

            if (outModel.irVersion.empty()) {
                outModel.irVersion = "unknown";
            }

            return Error{};
        }
        catch (const std::exception& e) {
            return Error(ErrorCode::ModelInvalid,
                std::string("ONNX parse failed: ") + e.what());
        }
    }

    Error OnnxModelParser::parseFromFile(const std::string& path,
        OnnxModel& outModel)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return Error(ErrorCode::FileNotFound,
                "Cannot open ONNX file: " + path);
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);

        return parse(data, outModel);
    }

    // ============================================================================
    // ModelRegistry
    // ============================================================================

    ModelRegistry::ModelRegistry() {
        scanAll();
    }

    void ModelRegistry::scanAll() {
        clear();

        // 扫描默认目录
        std::string defaultDir = ModelLoader::getDefaultModelDirectory();
        scanDirectory(defaultDir);

        // 扫描用户目录
        std::string userDir = ModelLoader::getUserModelDirectory();
        if (userDir != defaultDir) {
            scanDirectory(userDir);
        }

        // 注册内置模型
        registerBuiltinModels();

        LOG_INFO("ModelRegistry: %zu models found", entries_.size());
    }

    void ModelRegistry::scanDirectory(const std::string& dir) {
        auto models = ModelLoader::scanModels(dir);

        for (auto& info : models) {
            Entry entry;
            entry.name = fs::path(info.path).stem().string();
            entry.path = info.path;
            entry.format = info.format;
            entry.fileSize = info.fileSize;
            entry.available = true;

            // 从文件名推测精度
            std::string lower = entry.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("int8") != std::string::npos) {
                entry.precision = ModelPrecision::INT8;
            }
            else if (lower.find("fp16") != std::string::npos ||
                lower.find("half") != std::string::npos) {
                entry.precision = ModelPrecision::FP16;
            }
            else {
                entry.precision = ModelPrecision::FP32;
            }

            entry.displayName = entry.name;
            entry.description = "AI repair model";

            // 去重（同名时优先用户目录）
            auto existing = std::find_if(entries_.begin(), entries_.end(),
                [&](const Entry& e) { return e.name == entry.name; });

            if (existing == entries_.end()) {
                entries_.push_back(std::move(entry));
            }
        }
    }

    void ModelRegistry::registerBuiltinModels() {
        // 内置模型清单（若用户没有安装，显示为不可用）
        struct BuiltinModel {
            const char* name;
            const char* displayName;
            const char* description;
            bool recommended;
        };

        static const BuiltinModel builtins[] = {
            {
                "lingjing_repair_v1_fp16",
                "Lingjing Repair V1 (FP16)",
                "标准 AI 修复模型，平衡画质与性能",
                true
            },
            {
                "lingjing_repair_v1_int8",
                "Lingjing Repair V1 (INT8)",
                "轻量级 AI 修复，适用于低功耗 GPU",
                false
            },
            {
                "lingjing_repair_pro_fp16",
                "Lingjing Repair Pro (FP16)",
                "增强修复模型，适合高质量需求",
                false
            },
        };

        for (const auto& b : builtins) {
            // 检查是否已存在
            auto existing = std::find_if(entries_.begin(), entries_.end(),
                [&](const Entry& e) { return e.name == b.name; });

            if (existing != entries_.end()) {
                existing->displayName = b.displayName;
                existing->description = b.description;
                existing->recommended = b.recommended;
                continue;
            }

            // 添加为不可用条目
            Entry entry;
            entry.name = b.name;
            entry.displayName = b.displayName;
            entry.description = b.description;
            entry.recommended = b.recommended;
            entry.available = false;
            entries_.push_back(std::move(entry));
        }
    }

    const ModelRegistry::Entry* ModelRegistry::findByName(
        const std::string& name) const
    {
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [&](const Entry& e) { return e.name == name; });

        return (it != entries_.end()) ? &(*it) : nullptr;
    }

    const ModelRegistry::Entry* ModelRegistry::recommendedModel() const {
        // 优先返回可用的推荐模型
        for (const auto& e : entries_) {
            if (e.recommended && e.available) {
                return &e;
            }
        }

        // 次选：任何可用的模型
        for (const auto& e : entries_) {
            if (e.available) {
                return &e;
            }
        }

        return nullptr;
    }

} // namespace Lingjing
