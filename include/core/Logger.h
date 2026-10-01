#pragma once

#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>

namespace Lingjing {

    // ============================================================================
    // 日志级别
    // ============================================================================

    enum class LogLevel : uint32_t {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
        Off = 6,
    };

    const char* logLevelName(LogLevel level);
    LogLevel logLevelFromName(const std::string& name);

    // ============================================================================
    // 日志条目
    // ============================================================================

    struct LogEntry {
        LogLevel level = LogLevel::Info;
        std::string message;
        std::string file;
        int line = 0;
        std::string function;
        int64_t timestampMs = 0;
        uint32_t threadId = 0;
    };

    // ============================================================================
    // 日志回调
    // ============================================================================

    using LogCallback = std::function<void(const LogEntry&)>;

    // ============================================================================
    // 日志器
    // ============================================================================

    class Logger {
    public:
        static Logger& instance();

        // 初始化
        bool initialize(const std::string& logDir,
            const std::string& fileName = "lingjing.log");
        void shutdown();

        // 配置
        void setLevel(LogLevel level);
        LogLevel level() const { return level_; }

        void setConsoleOutput(bool enabled);
        void setFileOutput(bool enabled);

        // 回调
        void addCallback(LogCallback cb);
        void clearCallbacks();

        // 写日志
        void log(LogLevel level,
            const std::string& message,
            const char* file = nullptr,
            int line = 0,
            const char* function = nullptr);

        // 日志文件轮转
        void rotate();

    private:
        Logger();
        ~Logger();

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        void writeToFile(const LogEntry& entry);
        void writeToConsole(const LogEntry& entry);
        void notifyCallbacks(const LogEntry& entry);
        std::string formatEntry(const LogEntry& entry);

        LogLevel level_ = LogLevel::Info;
        bool consoleOutput_ = true;
        bool fileOutput_ = true;

        std::string logDir_;
        std::string logFileName_;
        std::string logFilePath_;

        std::ofstream fileStream_;
        mutable std::mutex mutex_;

        std::vector<LogCallback> callbacks_;

        // 文件轮转
        size_t maxFileSize_ = 16 * 1024 * 1024;  // 16 MB
        int maxBackupFiles_ = 5;
        size_t currentSize_ = 0;
    };

    // ============================================================================
    // 便捷宏
    // ============================================================================

#define LJ_LOG_TRACE(msg) \
    do { \
        std::ostringstream _oss; _oss << msg; \
        ::Lingjing::Logger::instance().log( \
            ::Lingjing::LogLevel::Trace, _oss.str(), \
            __FILE__, __LINE__, __func__); \
    } while (0)

#define LJ_LOG_DEBUG(msg) \
    do { \
        std::ostringstream _oss; _oss << msg; \
        ::Lingjing::Logger::instance().log( \
            ::Lingjing::LogLevel::Debug, _oss.str(), \
            __FILE__, __LINE__, __func__); \
    } while (0)

#define LJ_LOG_INFO(msg) \
    do { \
        std::ostringstream _oss; _oss << msg; \
        ::Lingjing::Logger::instance().log( \
            ::Lingjing::LogLevel::Info, _oss.str(), \
            __FILE__, __LINE__, __func__); \
    } while (0)

#define LJ_LOG_WARN(msg) \
    do { \
        std::ostringstream _oss; _oss << msg; \
        ::Lingjing::Logger::instance().log( \
            ::Lingjing::LogLevel::Warn, _oss.str(), \
            __FILE__, __LINE__, __func__); \
    } while (0)

#define LJ_LOG_ERROR(msg) \
    do { \
        std::ostringstream _oss; _oss << msg; \
        ::Lingjing::Logger::instance().log( \
            ::Lingjing::LogLevel::Error, _oss.str(), \
            __FILE__, __LINE__, __func__); \
    } while (0)

#define LJ_LOG_FATAL(msg) \
    do { \
        std::ostringstream _oss; _oss << msg; \
        ::Lingjing::Logger::instance().log( \
            ::Lingjing::LogLevel::Fatal, _oss.str(), \
            __FILE__, __LINE__, __func__); \
    } while (0)

} // namespace Lingjing