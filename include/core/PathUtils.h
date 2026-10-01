#pragma once

#include <string>
#include <filesystem>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 路径工具
    // ============================================================================

    class PathUtils {
    public:
        // ====================================================================
        // 应用路径
        // ====================================================================

        // 可执行文件路径
        static std::string executablePath();
        static std::string executableDir();

        // 应用数据目录（%APPDATA%/Lingjing）
        static std::string appDataDir();

        // 用户配置目录
        static std::string configDir();

        // 日志目录
        static std::string logDir();

        // 缓存目录
        static std::string cacheDir();

        // 模型目录
        static std::string modelsDir();
        static std::string userModelsDir();

        // 学习数据库路径
        static std::string learningDbPath();

        // 资源目录
        static std::string assetsDir();

        // ====================================================================
        // 操作
        // ====================================================================

        static bool ensureDirectory(const std::string& path);
        static bool exists(const std::string& path);
        static bool isFile(const std::string& path);
        static bool isDirectory(const std::string& path);

        static uint64_t fileSize(const std::string& path);
        static int64_t fileTimestamp(const std::string& path);

        static std::string readAllText(const std::string& path);
        static std::vector<uint8_t> readAllBytes(const std::string& path);

        static bool writeAllText(const std::string& path,
            const std::string& content);
        static bool writeAllBytes(const std::string& path,
            const std::vector<uint8_t>& data);

        static bool copyFile(const std::string& src,
            const std::string& dst,
            bool overwrite = true);
        static bool removeFile(const std::string& path);

        // ====================================================================
        // 路径操作
        // ====================================================================

        static std::string join(const std::string& a, const std::string& b);
        static std::string parent(const std::string& path);
        static std::string filename(const std::string& path);
        static std::string stem(const std::string& path);
        static std::string extension(const std::string& path);
        static std::string normalize(const std::string& path);

        // 文件名安全化（移除非法字符）
        static std::string sanitizeFilename(const std::string& name);

        // 临时文件
        static std::string tempDir();
        static std::string createTempFile(const std::string& prefix,
            const std::string& ext);
    };

} // namespace Lingjing