#include "core/StringUtils.h"

#include <cmath>
#include <ctime>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Lingjing {

    // ============================================================================
    // 大小写
    // ============================================================================

    std::string StringUtils::toLower(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::string StringUtils::toUpper(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

    std::wstring StringUtils::toWString(const std::string& s) {
#ifdef _WIN32
        if (s.empty()) return L"";

        int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
            static_cast<int>(s.size()),
            nullptr, 0);

        std::wstring result(size, 0);

        MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
            static_cast<int>(s.size()),
            result.data(), size);

        return result;
#else
        return std::wstring(s.begin(), s.end());
#endif
    }

    std::string StringUtils::toUtf8(const std::wstring& s) {
#ifdef _WIN32
        if (s.empty()) return "";

        int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(),
            static_cast<int>(s.size()),
            nullptr, 0, nullptr, nullptr);

        std::string result(size, 0);

        WideCharToMultiByte(CP_UTF8, 0, s.c_str(),
            static_cast<int>(s.size()),
            result.data(), size, nullptr, nullptr);

        return result;
#else
        return std::string(s.begin(), s.end());
#endif
    }

    // ============================================================================
    // 裁剪
    // ============================================================================

    std::string StringUtils::trim(const std::string& s) {
        return trimLeft(trimRight(s));
    }

    std::string StringUtils::trimLeft(const std::string& s) {
        auto it = std::find_if(s.begin(), s.end(),
            [](int c) { return !std::isspace(c); });
        return std::string(it, s.end());
    }

    std::string StringUtils::trimRight(const std::string& s) {
        auto it = std::find_if(s.rbegin(), s.rend(),
            [](int c) { return !std::isspace(c); });
        return std::string(s.begin(), it.base());
    }

    // ============================================================================
    // 分割/合并
    // ============================================================================

    std::vector<std::string> StringUtils::split(const std::string& s,
        char delimiter)
    {
        std::vector<std::string> result;
        std::istringstream iss(s);
        std::string token;

        while (std::getline(iss, token, delimiter)) {
            result.push_back(token);
        }

        return result;
    }

    std::vector<std::string> StringUtils::splitString(
        const std::string& s,
        const std::string& delimiter)
    {
        std::vector<std::string> result;

        if (delimiter.empty()) {
            result.push_back(s);
            return result;
        }

        size_t pos = 0;
        size_t prev = 0;

        while ((pos = s.find(delimiter, prev)) != std::string::npos) {
            result.push_back(s.substr(prev, pos - prev));
            prev = pos + delimiter.size();
        }

        result.push_back(s.substr(prev));

        return result;
    }

    std::string StringUtils::join(const std::vector<std::string>& parts,
        const std::string& separator)
    {
        std::ostringstream oss;

        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) oss << separator;
            oss << parts[i];
        }

        return oss.str();
    }

    // ============================================================================
    // 查找
    // ============================================================================

    bool StringUtils::startsWith(const std::string& s,
        const std::string& prefix)
    {
        if (s.size() < prefix.size()) return false;
        return s.compare(0, prefix.size(), prefix) == 0;
    }

    bool StringUtils::endsWith(const std::string& s,
        const std::string& suffix)
    {
        if (s.size() < suffix.size()) return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool StringUtils::contains(const std::string& s,
        const std::string& needle)
    {
        return s.find(needle) != std::string::npos;
    }

    std::string StringUtils::replace(const std::string& s,
        const std::string& from,
        const std::string& to)
    {
        size_t pos = s.find(from);

        if (pos == std::string::npos) return s;

        std::string result = s;
        result.replace(pos, from.size(), to);

        return result;
    }

    std::string StringUtils::replaceAll(const std::string& s,
        const std::string& from,
        const std::string& to)
    {
        if (from.empty()) return s;

        std::string result = s;
        size_t pos = 0;

        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.size(), to);
            pos += to.size();
        }

        return result;
    }

    // ============================================================================
    // 格式化
    // ============================================================================

    std::string StringUtils::formatFloat(float value, int precision) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        return oss.str();
    }

    std::string StringUtils::formatInt(int64_t value) {
        return std::to_string(value);
    }

    std::string StringUtils::formatUint(uint64_t value) {
        return std::to_string(value);
    }

    std::string StringUtils::formatHex(uint64_t value, int width) {
        std::ostringstream oss;
        oss << std::hex;

        if (width > 0) {
            oss << std::setw(width) << std::setfill('0');
        }

        oss << value;
        return oss.str();
    }

    bool StringUtils::parseInt(const std::string& s, int64_t& out) {
        try {
            size_t pos = 0;
            out = std::stoll(s, &pos);
            return pos == s.size();
        }
        catch (...) {
            return false;
        }
    }

    bool StringUtils::parseFloat(const std::string& s, double& out) {
        try {
            size_t pos = 0;
            out = std::stod(s, &pos);
            return pos == s.size();
        }
        catch (...) {
            return false;
        }
    }

    // ============================================================================
    // 时间格式化
    // ============================================================================

    std::string StringUtils::formatTimestamp(
        int64_t unixSeconds,
        const std::string& format)
    {
        if (unixSeconds <= 0) return "未知";

        std::time_t t = static_cast<std::time_t>(unixSeconds);
        std::tm tmBuf;

#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif

        char buf[128];
        std::strftime(buf, sizeof(buf), format.c_str(), &tmBuf);

        return buf;
    }

    std::string StringUtils::formatDuration(double seconds) {
        std::ostringstream oss;

        if (seconds < 60.0) {
            oss << std::fixed << std::setprecision(1) << seconds << " 秒";
        }
        else if (seconds < 3600.0) {
            int min = static_cast<int>(seconds / 60.0);
            int sec = static_cast<int>(seconds) % 60;
            oss << min << " 分 " << sec << " 秒";
        }
        else {
            int hours = static_cast<int>(seconds / 3600.0);
            int min = static_cast<int>((seconds - hours * 3600) / 60.0);
            oss << hours << " 小时 " << min << " 分钟";
        }

        return oss.str();
    }

    std::string StringUtils::formatBytes(uint64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unitIndex = 0;
        double value = static_cast<double>(bytes);

        while (value >= 1024.0 && unitIndex < 4) {
            value /= 1024.0;
            ++unitIndex;
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2)
            << value << " " << units[unitIndex];

        return oss.str();
    }

    // ============================================================================
    // 字节序
    // ============================================================================

    void StringUtils::writeInt32(std::vector<uint8_t>& buf, int32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    void StringUtils::writeUint32(std::vector<uint8_t>& buf, uint32_t v) {
        writeInt32(buf, static_cast<int32_t>(v));
    }

    void StringUtils::writeUint64(std::vector<uint8_t>& buf, uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    }

    void StringUtils::writeString(std::vector<uint8_t>& buf,
        const std::string& s)
    {
        writeUint32(buf, static_cast<uint32_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    }

    bool StringUtils::readInt32(const std::vector<uint8_t>& buf,
        size_t& offset, int32_t& out)
    {
        if (offset + 4 > buf.size()) return false;

        out = static_cast<int32_t>(buf[offset]) |
            (static_cast<int32_t>(buf[offset + 1]) << 8) |
            (static_cast<int32_t>(buf[offset + 2]) << 16) |
            (static_cast<int32_t>(buf[offset + 3]) << 24);

        offset += 4;
        return true;
    }

    bool StringUtils::readUint32(const std::vector<uint8_t>& buf,
        size_t& offset, uint32_t& out)
    {
        int32_t tmp;
        if (!readInt32(buf, offset, tmp)) return false;
        out = static_cast<uint32_t>(tmp);
        return true;
    }

    bool StringUtils::readUint64(const std::vector<uint8_t>& buf,
        size_t& offset, uint64_t& out)
    {
        if (offset + 8 > buf.size()) return false;

        out = 0;

        for (int i = 0; i < 8; ++i) {
            out |= static_cast<uint64_t>(buf[offset + i]) << (i * 8);
        }

        offset += 8;
        return true;
    }

    bool StringUtils::readString(const std::vector<uint8_t>& buf,
        size_t& offset, std::string& out)
    {
        uint32_t size = 0;
        if (!readUint32(buf, offset, size)) return false;

        if (offset + size > buf.size()) return false;

        out.assign(reinterpret_cast<const char*>(buf.data() + offset), size);
        offset += size;

        return true;
    }

} // namespace Lingjing