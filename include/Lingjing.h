#pragma once

// ============================================================================
// 灵境 Lingjing — 预编译头 (PCH)
// ============================================================================

#include "core/Logger.h"

#include <cstdio>
#include <cstdarg>
#include <string>

// ============================================================================
// printf 风格日志宏（源码中广泛使用 LOG_INFO / LOG_ERROR / LOG_WARN 等）
// ============================================================================

namespace Lingjing {
namespace detail {

inline void logPrintf(LogLevel level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Logger::instance().log(level, std::string(buf));
}

} // namespace detail
} // namespace Lingjing

#define LOG_TRACE(fmt, ...) ::Lingjing::detail::logPrintf(::Lingjing::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) ::Lingjing::detail::logPrintf(::Lingjing::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  ::Lingjing::detail::logPrintf(::Lingjing::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  ::Lingjing::detail::logPrintf(::Lingjing::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ::Lingjing::detail::logPrintf(::Lingjing::LogLevel::Error, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) ::Lingjing::detail::logPrintf(::Lingjing::LogLevel::Fatal, fmt, ##__VA_ARGS__)
