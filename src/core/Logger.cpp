#include "core/Logger.h"

#include <iostream>
#include <filesystem>
#include <iomanip>
#include <ctime>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Lingjing {

    // ============================================================================
    // 级别名称
    // ============================================================================

    const char* logLevelName(LogLevel level) {
        switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "?????";
        }
    }

    LogLevel logLevelFromName(const std::string& name) {
        if (name == "trace" || name == "TRACE") return LogLevel::Trace;
        if (name == "debug" || name == "DEBUG") return LogLevel::Debug;
        if (name == "info" || name == "INFO")  return LogLevel::Info;
        if (name == "warn" || name == "WARN")  return LogLevel::Warn;
        if (name == "error" || name == "ERROR") return LogLevel::Error;
        if (name == "fatal" || name == "FATAL") return LogLevel::Fatal;
        if (name == "off" || name == "OFF")   return LogLevel::Off;
        return LogLevel::Info;
    }

    // ============================================================================
    // 构造/析构
    // ============================================================================

    Logger::Logger() = default;

    Logger::~Logger() {
        shutdown();
    }

    Logger& Logger::instance() {
        static Logger inst;
        return inst;
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool Logger::initialize(const std::string& logDir,
        const std::string& fileName)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        logDir_ = logDir;
        logFileName_ = fileName;

        try {
            std::filesystem::create_directories(logDir);
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to create log directory: " << e.what() << "\n";
            return false;
        }

        logFilePath_ = logDir + "/" + logFileName_;

        // 打开文件（追加模式）
        fileStream_.open(logFilePath_, std::ios::out | std::ios::app);

        if (!fileStream_.is_open()) {
            std::cerr << "Failed to open log file: " << logFilePath_ << "\n";
            return false;
        }

        // 记录当前文件大小
        try {
            currentSize_ = std::filesystem::file_size(logFilePath_);
        }
        catch (...) {
            currentSize_ = 0;
        }

        return true;
    }

    void Logger::shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (fileStream_.is_open()) {
            fileStream_.flush();
            fileStream_.close();
        }
    }

    // ============================================================================
    // 配置
    // ============================================================================

    void Logger::setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    void Logger::setConsoleOutput(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        consoleOutput_ = enabled;
    }

    void Logger::setFileOutput(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        fileOutput_ = enabled;
    }

    void Logger::addCallback(LogCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(cb));
    }

    void Logger::clearCallbacks() {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.clear();
    }

    // ============================================================================
    // 格式化
    // ============================================================================

    static std::string formatTimestamp(int64_t timestampMs) {
        auto ms = timestampMs % 1000;
        auto timeSec = timestampMs / 1000;

        std::time_t t = static_cast<std::time_t>(timeSec);
        std::tm tmBuf;

#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif

        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);

        char result[80];
        std::snprintf(result, sizeof(result), "%s.%03lld",
            buf, static_cast<long long>(ms));

        return result;
    }

    static uint32_t currentThreadId() {
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return static_cast<uint32_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFFFFFF);
#endif
    }

    std::string Logger::formatEntry(const LogEntry& entry) {
        std::ostringstream oss;

        oss << "[" << formatTimestamp(entry.timestampMs) << "] "
            << "[" << logLevelName(entry.level) << "] "
            << "[T" << entry.threadId << "] ";

        if (!entry.file.empty()) {
            // 只显示文件名
            const char* fname = entry.file.c_str();
            const char* p = fname;
            for (const char* q = fname; *q; ++q) {
                if (*q == '/' || *q == '\\') p = q + 1;
            }

            oss << "[" << p << ":" << entry.line << "] ";
        }

        oss << entry.message;

        return oss.str();
    }

    // ============================================================================
    // 写日志
    // ============================================================================

    void Logger::log(LogLevel level,
        const std::string& message,
        const char* file,
        int line,
        const char* function)
    {
        // 快速检查级别（避免加锁）
        if (level < level_) return;

        LogEntry entry;
        entry.level = level;
        entry.message = message;
        entry.file = file ? file : "";
        entry.line = line;
        entry.function = function ? function : "";
        entry.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        entry.threadId = currentThreadId();

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (fileOutput_ && fileStream_.is_open()) {
                writeToFile(entry);
            }

            if (consoleOutput_) {
                writeToConsole(entry);
            }

            notifyCallbacks(entry);
        }
    }

    void Logger::writeToFile(const LogEntry& entry) {
        std::string line = formatEntry(entry) + "\n";

        fileStream_ << line;
        fileStream_.flush();

        currentSize_ += line.size();

        if (currentSize_ >= maxFileSize_) {
            // 关闭当前文件
            fileStream_.close();

            // 轮转：.log → .log.1 → .log.2 ...
            try {
                for (int i = maxBackupFiles_ - 1; i >= 1; --i) {
                    std::string oldPath = logFilePath_ + "." + std::to_string(i);
                    std::string newPath = logFilePath_ + "." + std::to_string(i + 1);

                    if (std::filesystem::exists(oldPath)) {
                        if (std::filesystem::exists(newPath)) {
                            std::filesystem::remove(newPath);
                        }
                        std::filesystem::rename(oldPath, newPath);
                    }
                }

                std::string firstBackup = logFilePath_ + ".1";
                if (std::filesystem::exists(logFilePath_)) {
                    std::filesystem::rename(logFilePath_, firstBackup);
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Log rotation failed: " << e.what() << "\n";
            }

            // 重新打开
            fileStream_.open(logFilePath_, std::ios::out | std::ios::trunc);
            currentSize_ = 0;
        }
    }

    void Logger::writeToConsole(const LogEntry& entry) {
        std::string line = formatEntry(entry);

        // 根据级别着色
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

        switch (entry.level) {
        case LogLevel::Trace: color = FOREGROUND_INTENSITY; break;
        case LogLevel::Debug: color = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
        case LogLevel::Info:  color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
        case LogLevel::Warn:  color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
        case LogLevel::Error: color = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
        case LogLevel::Fatal: color = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
        default: break;
        }

        SetConsoleTextAttribute(hConsole, color);
        std::cerr << line << "\n";
        SetConsoleTextAttribute(hConsole,
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
        std::cerr << line << "\n";
#endif
    }

    void Logger::notifyCallbacks(const LogEntry& entry) {
        for (const auto& cb : callbacks_) {
            try {
                cb(entry);
            }
            catch (...) {
                // 忽略回调中的异常
            }
        }
    }

    void Logger::rotate() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!fileStream_.is_open()) return;

        fileStream_.flush();
        fileStream_.close();

        try {
            std::string rotated = logFilePath_ + ".old";
            if (std::filesystem::exists(rotated)) {
                std::filesystem::remove(rotated);
            }
            if (std::filesystem::exists(logFilePath_)) {
                std::filesystem::rename(logFilePath_, rotated);
            }
        }
        catch (...) {
            // 忽略轮转错误
        }

        fileStream_.open(logFilePath_, std::ios::out | std::ios::trunc);
        currentSize_ = 0;
    }

} // namespace Lingjing