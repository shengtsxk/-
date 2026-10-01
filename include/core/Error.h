#pragma once

#include "Types.h"
#include <string>
#include <exception>
#include <functional>

namespace Lingjing {

    // ============================================================================
    // 异常类
    // ============================================================================

    class LingjingException : public std::exception {
    public:
        LingjingException(ErrorCode code, const std::string& message)
            : code_(code), message_(message) {
        }

        LingjingException(ErrorCode code,
            const std::string& message,
            const std::string& detail)
            : code_(code), message_(message), detail_(detail) {
        }

        const char* what() const noexcept override {
            return message_.c_str();
        }

        ErrorCode code() const { return code_; }
        const std::string& message() const { return message_; }
        const std::string& detail() const { return detail_; }

    private:
        ErrorCode code_;
        std::string message_;
        std::string detail_;
    };

    // ============================================================================
    // 错误处理辅助
    // ============================================================================

    inline Error makeSuccess() {
        return Error{};
    }

    inline Error makeError(ErrorCode code, const std::string& msg) {
        return Error{ code, msg };
    }

    template<typename T>
    struct Result {
        bool ok = false;
        T value{};
        Error error;

        bool isOk() const { return ok; }
        bool isErr() const { return !ok; }

        T& operator*() { return value; }
        const T& operator*() const { return value; }

        T* operator->() { return &value; }
        const T* operator->() const { return &value; }

        static Result success(const T& v) {
            Result r;
            r.ok = true;
            r.value = v;
            return r;
        }

        static Result failure(ErrorCode code, const std::string& msg) {
            Result r;
            r.ok = false;
            r.error = Error{ code, msg };
            return r;
        }

        static Result failure(const Error& e) {
            Result r;
            r.ok = false;
            r.error = e;
            return r;
        }
    };

    // ============================================================================
    // 错误处理钩子
    // ============================================================================

    using ErrorHandler = std::function<void(const Error&)>;

    void setGlobalErrorHandler(ErrorHandler handler);
    void reportError(const Error& error);

    // ============================================================================
    // 检查宏
    // ============================================================================

#define LJ_TRY(expr) \
    do { \
        auto _result = (expr); \
        if (!_result.isOk()) { \
            return _result; \
        } \
    } while (0)

#define LJ_CHECK(cond, code, msg) \
    do { \
        if (!(cond)) { \
            return ::Lingjing::Result<void>::failure(code, msg); \
        } \
    } while (0)

#define LJ_THROW(code, msg) \
    throw ::Lingjing::LingjingException(code, msg)

} // namespace Lingjing