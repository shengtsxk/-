#include "core/Error.h"
#include "core/Logger.h"

#include <mutex>

namespace Lingjing {

    const char* errorCodeName(ErrorCode code) {
        switch (code) {
        case ErrorCode::Success:              return "Success";
        case ErrorCode::SystemError:          return "System Error";
        case ErrorCode::OutOfMemory:          return "Out of Memory";
        case ErrorCode::FileNotFound:         return "File Not Found";
        case ErrorCode::PermissionDenied:     return "Permission Denied";
        case ErrorCode::InvalidArgument:      return "Invalid Argument";
        case ErrorCode::CaptureInitFailed:    return "Capture Init Failed";
        case ErrorCode::CaptureLost:          return "Capture Lost";
        case ErrorCode::CaptureTimeout:       return "Capture Timeout";
        case ErrorCode::WindowNotFound:       return "Window Not Found";
        case ErrorCode::GpuNotFound:          return "GPU Not Found";
        case ErrorCode::GpuInitFailed:        return "GPU Init Failed";
        case ErrorCode::GpuOutOfMemory:       return "GPU Out of Memory";
        case ErrorCode::CudaError:            return "CUDA Error";
        case ErrorCode::SyclError:            return "SYCL Error";
        case ErrorCode::D3DError:             return "D3D Error";
        case ErrorCode::FlowInitFailed:       return "Flow Init Failed";
        case ErrorCode::FlowSolveFailed:      return "Flow Solve Failed";
        case ErrorCode::FlowEngineUnavailable:return "Flow Engine Unavailable";
        case ErrorCode::ModelLoadFailed:      return "Model Load Failed";
        case ErrorCode::ModelInvalid:         return "Model Invalid";
        case ErrorCode::InferenceFailed:      return "Inference Failed";
        case ErrorCode::InferenceTimeout:     return "Inference Timeout";
        case ErrorCode::LearningDbCorrupted:  return "Learning DB Corrupted";
        case ErrorCode::LearningDbAccessDenied:return "Learning DB Access Denied";
        case ErrorCode::PipelineNotInitialized:return "Pipeline Not Initialized";
        case ErrorCode::PipelineAlreadyRunning:return "Pipeline Already Running";
        case ErrorCode::PipelineCrashed:      return "Pipeline Crashed";
        default: return "Unknown Error";
        }
    }

    // ============================================================================
    // 全局错误处理器
    // ============================================================================

    static std::mutex g_errorHandlerMutex;
    static ErrorHandler g_errorHandler;

    void setGlobalErrorHandler(ErrorHandler handler) {
        std::lock_guard<std::mutex> lock(g_errorHandlerMutex);
        g_errorHandler = std::move(handler);
    }

    void reportError(const Error& error) {
        if (error.isSuccess()) return;

        LOG_ERROR("[%s] %s%s%s",
            errorCodeName(error.code),
            error.message.c_str(),
            error.detail.empty() ? "" : " | ",
            error.detail.c_str());

        ErrorHandler handler;
        {
            std::lock_guard<std::mutex> lock(g_errorHandlerMutex);
            handler = g_errorHandler;
        }

        if (handler) {
            try {
                handler(error);
            }
            catch (...) {
                // 忽略处理器异常
            }
        }
    }

} // namespace Lingjing