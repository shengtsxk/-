#include "core/PathUtils.h"
#include "core/Logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace fs = std::filesystem;

namespace Lingjing {

    // ============================================================================
    // 可执行文件路径
    // ============================================================================

    std::string PathUtils::executablePath() {
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);

        if (len > 0) {
            return std::string(buf, len);
        }
#else
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);

        if (len > 0) {
            buf[len] = '\0';
            return std::string(buf);
        }
#endif

        return "";
    }

    std::string PathUtils::executableDir() {
        std::string exe = executablePath();
        return parent(exe);
    }

    // ============================================================================
    // 应用数据目录
    // ============================================================================

    std::string PathUtils::appDataDir() {
#ifdef _WIN32
        char path[MAX_PATH];

        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA,
            nullptr, 0, path))) {
            std::string result = join(path, "Lingjing");
            ensureDirectory(result);
            return result;
        }
#endif

        // 回退
        std::string result = join(executableDir(), "data");
        ensureDirectory(result);
        return result;
    }

    std::string PathUtils::configDir() {
        std::string result = join(appDataDir(), "config");
        ensureDirectory(result);
        return result;
    }

    std::string PathUtils::logDir() {
        std::string result = join(appDataDir(), "logs");
        ensureDirectory(result);
        return result;
    }

    std::string PathUtils::cacheDir() {
        std::string result = join(appDataDir(), "cache");
        ensureDirectory(result);
        return result;
    }

    std::string PathUtils::modelsDir() {
        // 应用目录下的 models
        std::string result = join(executableDir(), "models");
        ensureDirectory(result);
        return result;
    }

    std::string PathUtils::userModelsDir() {
        std::string result = join(appDataDir(), "models");
        ensureDirectory(result);
        return result;
    }

    std::string PathUtils::learningDbPath() {
        return join(appDataDir(), "learning.db");
    }

    std::string PathUtils::assetsDir() {
        std::string result = join(executableDir(), "assets");
        ensureDirectory(result);
        return result;
    }

    // ============================================================================
    // 目录操作
    // ============================================================================

    bool PathUtils::ensureDirectory(const std::string& path) {
        try {
            if (fs::exists(path)) return true;
            return fs::create_directories(path);
        }
        catch (const std::exception& e) {
            LOG_WARN("Failed to create directory %s: %s",
                path.c_str(), e.what());
            return false;
        }
    }

    bool PathUtils::exists(const std::string& path) {
        try {
            return fs::exists(path);
        }
        catch (...) {
            return false;
        }
    }

    bool PathUtils::isFile(const std::string& path) {
        try {
            return fs::is_regular_file(path);
        }
        catch (...) {
            return false;
        }
    }

    bool PathUtils::isDirectory(const std::string& path) {
        try {
            return fs::is_directory(path);
        }
        catch (...) {
            return false;
        }
    }

    uint64_t PathUtils::fileSize(const std::string& path) {
        try {
            return fs::file_size(path);
        }
        catch (...) {
            return 0;
        }
    }

    int64_t PathUtils::fileTimestamp(const std::string& path) {
        try {
            auto ftime = fs::last_write_time(path);
            return std::chrono::duration_cast<std::chrono::seconds>(
                ftime.time_since_epoch()).count();
        }
        catch (...) {
            return 0;
        }
    }

    // ============================================================================
    // 文件读写
    // ============================================================================

    std::string PathUtils::readAllText(const std::string& path) {
        std::ifstream f(path, std::ios::binary);

        if (!f.is_open()) return "";

        std::ostringstream oss;
        oss << f.rdbuf();

        return oss.str();
    }

    std::vector<uint8_t> PathUtils::readAllBytes(const std::string& path) {
        std::vector<uint8_t> result;

        std::ifstream f(path, std::ios::binary | std::ios::ate);

        if (!f.is_open()) return result;

        size_t size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        result.resize(size);
        f.read(reinterpret_cast<char*>(result.data()), size);

        return result;
    }

    bool PathUtils::writeAllText(const std::string& path,
        const std::string& content)
    {
        std::ofstream f(path, std::ios::out | std::ios::trunc);

        if (!f.is_open()) return false;

        f.write(content.data(), content.size());

        return f.good();
    }

    bool PathUtils::writeAllBytes(const std::string& path,
        const std::vector<uint8_t>& data)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);

        if (!f.is_open()) return false;

        f.write(reinterpret_cast<const char*>(data.data()), data.size());

        return f.good();
    }

    bool PathUtils::copyFile(const std::string& src,
        const std::string& dst,
        bool overwrite)
    {
        try {
            auto options = overwrite
                ? fs::copy_options::overwrite_existing
                : fs::copy_options::none;

            return fs::copy_file(src, dst, options);
        }
        catch (...) {
            return false;
        }
    }

    bool PathUtils::removeFile(const std::string& path) {
        try {
            return fs::remove(path);
        }
        catch (...) {
            return false;
        }
    }

    // ============================================================================
    // 路径操作
    // ============================================================================

    std::string PathUtils::join(const std::string& a, const std::string& b) {
        return (fs::path(a) / b).string();
    }

    std::string PathUtils::parent(const std::string& path) {
        return fs::path(path).parent_path().string();
    }

    std::string PathUtils::filename(const std::string& path) {
        return fs::path(path).filename().string();
    }

    std::string PathUtils::stem(const std::string& path) {
        return fs::path(path).stem().string();
    }

    std::string PathUtils::extension(const std::string& path) {
        return fs::path(path).extension().string();
    }

    std::string PathUtils::normalize(const std::string& path) {
        try {
            return fs::path(path).lexically_normal().string();
        }
        catch (...) {
            return path;
        }
    }

    std::string PathUtils::sanitizeFilename(const std::string& name) {
        std::string result = name;

        // 非法字符
        const char* invalid = "<>:\"/\\|?*";

        for (char& c : result) {
            if (std::strchr(invalid, c)) {
                c = '_';
            }
        }

        // 移除首尾空格和点
        while (!result.empty() && (result.front() == ' ' ||
            result.front() == '.')) {
            result.erase(result.begin());
        }

        while (!result.empty() && (result.back() == ' ' ||
            result.back() == '.')) {
            result.pop_back();
        }

        // 空名回退
        if (result.empty()) {
            result = "unnamed";
        }

        // 长度限制
        if (result.size() > 200) {
            result.resize(200);
        }

        return result;
    }

    std::string PathUtils::tempDir() {
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD len = GetTempPathA(MAX_PATH, buf);

        if (len > 0) {
            return std::string(buf, len);
        }
#endif

        return appDataDir();
    }

    std::string PathUtils::createTempFile(const std::string& prefix,
        const std::string& ext)
    {
        static std::mt19937_64 rng(std::random_device{}());

        uint64_t id = rng();

        std::ostringstream oss;
        oss << prefix << "_" << std::hex << id << ext;

        return join(tempDir(), oss.str());
    }

} // namespace Lingjing

// ============================================================================
