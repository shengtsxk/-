#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace Lingjing {

    // ============================================================================
    // 基础几何类型
    // ============================================================================

    struct Vec2f {
        float x = 0.0f;
        float y = 0.0f;

        Vec2f() = default;
        Vec2f(float xx, float yy) : x(xx), y(yy) {}

        Vec2f operator+(const Vec2f& o) const { return { x + o.x, y + o.y }; }
        Vec2f operator-(const Vec2f& o) const { return { x - o.x, y - o.y }; }
        Vec2f operator*(float s) const { return { x * s, y * s }; }
        float dot(const Vec2f& o) const { return x * o.x + y * o.y; }
        float length() const { return sqrtf(x * x + y * y); }
    };

    struct Vec3f {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Vec4f {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct Rect {
        int32_t x = 0;
        int32_t y = 0;
        int32_t width = 0;
        int32_t height = 0;

        bool valid() const { return width > 0 && height > 0; }
        int32_t area() const { return width * height; }
        int32_t right() const { return x + width; }
        int32_t bottom() const { return y + height; }

        bool contains(int32_t px, int32_t py) const {
            return px >= x && px < right() && py >= y && py < bottom();
        }

        Rect intersect(const Rect& other) const {
            Rect r;
            r.x = (std::max)(x, other.x);
            r.y = (std::max)(y, other.y);
            int32_t r2 = (std::min)(right(), other.right());
            int32_t b2 = (std::min)(bottom(), other.bottom());
            r.width = r2 - r.x;
            r.height = b2 - r.y;
            if (r.width < 0) r.width = 0;
            if (r.height < 0) r.height = 0;
            return r;
        }
    };

    struct Size {
        uint32_t width = 0;
        uint32_t height = 0;

        bool valid() const { return width > 0 && height > 0; }
        uint64_t pixels() const {
            return static_cast<uint64_t>(width) * height;
        }
    };

    // ============================================================================
    // 时间戳
    // ============================================================================

    using TimestampNs = int64_t;
    using FrameIndex = uint64_t;

    inline TimestampNs nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // ============================================================================
    // 纹理格式
    // ============================================================================

    enum class TextureFormat : uint32_t {
        Unknown = 0,

        // 8-bit 格式
        R8_UNORM,
        R8G8_UNORM,
        R8G8B8A8_UNORM,
        B8G8R8A8_UNORM,

        // 10-bit HDR
        R10G10B10A2_UNORM,

        // 16-bit 浮点
        R16_FLOAT,
        R16G16_FLOAT,
        R16G16B16A16_FLOAT,

        // 32-bit 浮点
        R32_FLOAT,
        R32G32_FLOAT,
        R32G32B32_FLOAT,
        R32G32B32A32_FLOAT,
    };

    inline uint32_t bytesPerPixel(TextureFormat fmt) {
        switch (fmt) {
        case TextureFormat::R8_UNORM:              return 1;
        case TextureFormat::R8G8_UNORM:            return 2;
        case TextureFormat::R8G8B8A8_UNORM:        return 4;
        case TextureFormat::B8G8R8A8_UNORM:        return 4;
        case TextureFormat::R10G10B10A2_UNORM:     return 4;
        case TextureFormat::R16_FLOAT:             return 2;
        case TextureFormat::R16G16_FLOAT:          return 4;
        case TextureFormat::R16G16B16A16_FLOAT:    return 8;
        case TextureFormat::R32_FLOAT:             return 4;
        case TextureFormat::R32G32_FLOAT:          return 8;
        case TextureFormat::R32G32B32_FLOAT:       return 12;
        case TextureFormat::R32G32B32A32_FLOAT:    return 16;
        default: return 0;
        }
    }

    inline const char* textureFormatName(TextureFormat fmt) {
        switch (fmt) {
        case TextureFormat::R8_UNORM:              return "R8_UNORM";
        case TextureFormat::R8G8_UNORM:            return "R8G8_UNORM";
        case TextureFormat::R8G8B8A8_UNORM:        return "R8G8B8A8_UNORM";
        case TextureFormat::B8G8R8A8_UNORM:        return "B8G8R8A8_UNORM";
        case TextureFormat::R10G10B10A2_UNORM:     return "R10G10B10A2_UNORM";
        case TextureFormat::R16_FLOAT:             return "R16_FLOAT";
        case TextureFormat::R16G16_FLOAT:          return "R16G16_FLOAT";
        case TextureFormat::R16G16B16A16_FLOAT:    return "R16G16B16A16_FLOAT";
        case TextureFormat::R32_FLOAT:             return "R32_FLOAT";
        case TextureFormat::R32G32_FLOAT:          return "R32G32_FLOAT";
        case TextureFormat::R32G32B32_FLOAT:       return "R32G32B32_FLOAT";
        case TextureFormat::R32G32B32A32_FLOAT:    return "R32G32B32A32_FLOAT";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // GPU 纹理句柄
    // ============================================================================

    struct GpuTexture {
        void* nativeHandle = nullptr;      // ID3D11Texture2D* / ID3D12Resource* / cl_mem / CPU 像素缓冲
        void* sharedHandle = nullptr;      // 共享句柄（跨进程）
        bool isCpuBuffer = false;          // nativeHandle 指向 CPU 像素内存（读回缓冲），而非 GPU 纹理

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitch = 0;
        uint32_t depthPitch = 0;

        TextureFormat format = TextureFormat::Unknown;

        FrameIndex frameIndex = 0;
        TimestampNs timestampNs = 0;

        bool valid() const {
            return nativeHandle != nullptr && width > 0 && height > 0;
        }

        uint64_t approximateBytes() const {
            return static_cast<uint64_t>(rowPitch) * height;
        }

        void reset() {
            nativeHandle = nullptr;
            sharedHandle = nullptr;
            isCpuBuffer = false;
            width = 0;
            height = 0;
            rowPitch = 0;
            depthPitch = 0;
            format = TextureFormat::Unknown;
            frameIndex = 0;
            timestampNs = 0;
        }
    };

    // ============================================================================
    // 捕获相关
    // ============================================================================

    enum class CaptureBackend : uint32_t {
        Auto = 0,
        WGC,           // Windows Graphics Capture
        DXGI_DD,       // Desktop Duplication
        Hook,          // 图形钩子注入
    };

    inline const char* captureBackendName(CaptureBackend backend) {
        switch (backend) {
        case CaptureBackend::WGC:     return "Windows Graphics Capture";
        case CaptureBackend::DXGI_DD: return "DXGI Desktop Duplication";
        case CaptureBackend::Hook:    return "Graphics API Hook";
        default: return "Auto";
        }
    }

    enum class WindowMode : uint32_t {
        Unknown = 0,
        Windowed,               // 普通窗口
        Borderless,             // 无边框窗口
        Fullscreen,             // 独占全屏
        BorderlessFullscreen,   // 无边框全屏
    };

    inline const char* windowModeName(WindowMode mode) {
        switch (mode) {
        case WindowMode::Windowed:             return "Windowed";
        case WindowMode::Borderless:           return "Borderless";
        case WindowMode::Fullscreen:           return "Fullscreen";
        case WindowMode::BorderlessFullscreen: return "Borderless Fullscreen";
        default: return "Unknown";
        }
    }

    struct CaptureTarget {
        void* hwnd = nullptr;
        std::wstring windowTitle;
        std::wstring className;
        std::wstring processName;
        std::wstring processPath;
        uint32_t processId = 0;
        WindowMode mode = WindowMode::Unknown;
        Rect clientRect;
        Size frameSize;
        bool isForeground = false;
        bool isMinimized = false;
        bool isMultiMonitor = false;

        bool valid() const { return hwnd != nullptr; }
    };

    // ============================================================================
    // 帧生成档位
    // ============================================================================

    enum class QualityLevel : uint32_t {
        Performance = 0,   // 最快，画质稍差
        Balanced = 1,   // 平衡
        Quality = 2,   // 高画质
        Ultra = 3,   // 极致（AI 修复 + 最高画质）
    };

    inline const char* qualityLevelName(QualityLevel q) {
        switch (q) {
        case QualityLevel::Performance: return "Performance";
        case QualityLevel::Balanced:    return "Balanced";
        case QualityLevel::Quality:     return "Quality";
        case QualityLevel::Ultra:       return "Ultra";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // 帧生成配置
    // ============================================================================

    struct FrameGenSettings {
        bool enabled = true;
        uint32_t multiplier = 2;          // 1~20 倍
        QualityLevel quality = QualityLevel::Balanced;

        // 遮挡处理
        bool occlusionAware = true;
        float occlusionThreshold = 0.55f;

        // 去果冻
        bool deJellyEnabled = true;
        float deJellyStrength = 0.6f;

        // AI 修复
        bool aiRepairEnabled = true;
        std::string aiRepairModelPath;

        // 时域平滑
        bool temporalSmoothing = true;
        float temporalWeight = 0.15f;

        // 光流参数
        float flowLambda = 0.30f;
        float flowGamma = 1.50f;
        int flowIterCoarse = 24;
        int flowIterFine = 8;

        // 淮竹算法
        bool huaiZhuEnabled = true;
        int huaiZhuWarmupFrames = 30;
        float huaiZhuConvergence = 0.95f;

        // 学习系统
        bool learningEnabled = true;

        // 性能限制
        float maxFrameTimeMs = 8.0f;      // 单帧最大耗时
        bool adaptiveQuality = true;       // 自适应降档
    };

    // ============================================================================
    // 性能统计
    // ============================================================================

    struct FrameStats {
        // 各阶段耗时（ms）
        float captureMs = 0.0f;
        float flowSolveMs = 0.0f;
        float geometryMs = 0.0f;
        float occlusionMs = 0.0f;
        float aiRepairMs = 0.0f;
        float deJellyMs = 0.0f;
        float blendMs = 0.0f;
        float presentMs = 0.0f;
        float totalMs = 0.0f;

        // 帧率
        float sourceFps = 0.0f;
        float outputFps = 0.0f;

        // 计数
        uint64_t capturedFrames = 0;
        uint64_t generatedFrames = 0;
        uint64_t droppedFrames = 0;

        // GPU 状态
        float gpuUtilization = 0.0f;
        float vramUsageMB = 0.0f;
        float gpuTempC = 0.0f;

        void reset() {
            captureMs = 0.0f;
            flowSolveMs = 0.0f;
            geometryMs = 0.0f;
            occlusionMs = 0.0f;
            aiRepairMs = 0.0f;
            deJellyMs = 0.0f;
            blendMs = 0.0f;
            presentMs = 0.0f;
            totalMs = 0.0f;
        }

        void resetCounters() {
            capturedFrames = 0;
            generatedFrames = 0;
            droppedFrames = 0;
        }
    };

    // ============================================================================
    // 异常与错误
    // ============================================================================

    enum class ErrorCode : uint32_t {
        Success = 0,

        // 系统错误
        SystemError,
        OutOfMemory,
        FileNotFound,
        PermissionDenied,
        InvalidArgument,

        // 捕获错误
        CaptureInitFailed,
        CaptureLost,
        CaptureTimeout,
        WindowNotFound,

        // GPU 错误
        GpuNotFound,
        GpuInitFailed,
        GpuOutOfMemory,
        CudaError,
        SyclError,
        D3DError,

        // 光流错误
        FlowInitFailed,
        FlowSolveFailed,
        FlowEngineUnavailable,

        // AI 错误
        ModelLoadFailed,
        ModelInvalid,
        InferenceFailed,
        InferenceTimeout,

        // 学习系统错误
        LearningDbCorrupted,
        LearningDbAccessDenied,

        // 管线错误
        PipelineNotInitialized,
        PipelineAlreadyRunning,
        PipelineCrashed,
    };

    const char* errorCodeName(ErrorCode code);

    struct Error {
        ErrorCode code = ErrorCode::Success;
        std::string message;
        std::string detail;
        TimestampNs timestamp = 0;

        Error() = default;
        Error(ErrorCode c, const std::string& msg)
            : code(c), message(msg), timestamp(nowNs()) {
        }

        bool isSuccess() const { return code == ErrorCode::Success; }
        bool isFailure() const { return code != ErrorCode::Success; }
    };

    // ============================================================================
    // 通用句柄（RAII）
    // ============================================================================

    template<typename T, typename Deleter>
    class ScopedHandle {
    public:
        ScopedHandle() = default;
        explicit ScopedHandle(T handle) : handle_(handle) {}
        ~ScopedHandle() { reset(); }

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        ScopedHandle(ScopedHandle&& other) noexcept
            : handle_(other.handle_) {
            other.handle_ = T{};
        }

        ScopedHandle& operator=(ScopedHandle&& other) noexcept {
            if (this != &other) {
                reset();
                handle_ = other.handle_;
                other.handle_ = T{};
            }
            return *this;
        }

        void reset() {
            if (handle_) {
                Deleter{}(handle_);
                handle_ = T{};
            }
        }

        T get() const { return handle_; }
        T release() {
            T h = handle_;
            handle_ = T{};
            return h;
        }

        explicit operator bool() const { return handle_ != T{}; }

    private:
        T handle_{};
    };

} // namespace Lingjing