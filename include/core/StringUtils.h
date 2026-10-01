#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>

namespace Lingjing {

    // ============================================================================
    // 字符串工具
    // ============================================================================

    class StringUtils {
    public:
        // ====================================================================
        // 大小写
        // ====================================================================

        static std::string toLower(const std::string& s);
        static std::string toUpper(const std::string& s);

        static std::wstring toWString(const std::string& s);
        static std::string toUtf8(const std::wstring& s);

        // ====================================================================
        // 裁剪
        // ====================================================================

        static std::string trim(const std::string& s);
        static std::string trimLeft(const std::string& s);
        static std::string trimRight(const std::string& s);

        // ====================================================================
        // 分割/合并
        // ====================================================================

        static std::vector<std::string> split(const std::string& s,
            char delimiter);
        static std::vector<std::string> splitString(const std::string& s,
            const std::string& delimiter);

        static std::string join(const std::vector<std::string>& parts,
            const std::string& separator);

        // ====================================================================
        // 查找/替换
        // ====================================================================

        static bool startsWith(const std::string& s, const std::string& prefix);
        static bool endsWith(const std::string& s, const std::string& suffix);
        static bool contains(const std::string& s, const std::string& needle);

        static std::string replace(const std::string& s,
            const std::string& from,
            const std::string& to);
        static std::string replaceAll(const std::string& s,
            const std::string& from,
            const std::string& to);

        // ====================================================================
        // 数字转换
        // ====================================================================

        static std::string formatFloat(float value, int precision = 2);
        static std::string formatInt(int64_t value);
        static std::string formatUint(uint64_t value);
        static std::string formatHex(uint64_t value, int width = 0);

        static bool parseInt(const std::string& s, int64_t& out);
        static bool parseFloat(const std::string& s, double& out);

        // ====================================================================
        // 时间格式化
        // ====================================================================

        static std::string formatTimestamp(int64_t unixSeconds,
            const std::string& format
            = "%Y-%m-%d %H:%M:%S");
        static std::string formatDuration(double seconds);
        static std::string formatBytes(uint64_t bytes);

        // ====================================================================
        // 字节序
        // ====================================================================

        static void writeInt32(std::vector<uint8_t>& buf, int32_t v);
        static void writeUint32(std::vector<uint8_t>& buf, uint32_t v);
        static void writeUint64(std::vector<uint8_t>& buf, uint64_t v);
        static void writeString(std::vector<uint8_t>& buf,
            const std::string& s);

        static bool readInt32(const std::vector<uint8_t>& buf,
            size_t& offset, int32_t& out);
        static bool readUint32(const std::vector<uint8_t>& buf,
            size_t& offset, uint32_t& out);
        static bool readUint64(const std::vector<uint8_t>& buf,
            size_t& offset, uint64_t& out);
        static bool readString(const std::vector<uint8_t>& buf,
            size_t& offset, std::string& out);
    };

} // namespace Lingjing