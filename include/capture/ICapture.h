#pragma once

#include "core/Types.h"
#include <functional>
#include <memory>
#include <string>

namespace Lingjing {

    // ============================================================================
    // 帧回调
    // ============================================================================

    using FrameCapturedCallback = std::function<void(const GpuTexture& frame)>;
    using CaptureErrorCallback = std::function<void(const Error& error)>;
    using CaptureStateCallback = std::function<void(bool running)>;

    // ============================================================================
    // 捕获配置
    // ============================================================================

    struct CaptureConfig {
        // 目标
        void* targetHwnd = nullptr;
        uint32_t targetOutputIndex = 0;   // 用于 DXGI 全屏

        // 后端选择
        CaptureBackend preferredBackend = CaptureBackend::Auto;

        // 帧率限制（0 = 不限制）
        uint32_t maxFps = 0;

        // 光标
        bool captureCursor = false;

        // 格式
        bool allowFormatConversion = true;
        TextureFormat preferredFormat = TextureFormat::Unknown;

        // 缓冲
        uint32_t ringBufferSize = 3;
        uint32_t timeoutMs = 16;

        // 共享纹理（跨进程零拷贝）
        bool useSharedTexture = true;

        // 强制重初始化
        uint32_t maxReinitAttempts = 3;
        uint32_t reinitDelayMs = 100;

        // 彩色（vs 灰度）
        bool captureColor = true;

        // HDR 支持
        bool enableHDR = false;
        float hdrPeakNits = 1000.0f;
    };

    // ============================================================================
    // 捕获统计
    // ============================================================================

    struct CaptureStats {
        uint64_t capturedFrames = 0;
        uint64_t droppedFrames = 0;
        uint64_t reinitCount = 0;
        uint64_t totalBytesCaptured = 0;
        float avgCaptureTimeMs = 0.0f;
        float maxCaptureTimeMs = 0.0f;
        float lastCaptureTimeMs = 0.0f;
        float fps = 0.0f;

        void reset() {
            capturedFrames = 0;
            droppedFrames = 0;
            reinitCount = 0;
            totalBytesCaptured = 0;
            avgCaptureTimeMs = 0.0f;
            maxCaptureTimeMs = 0.0f;
            lastCaptureTimeMs = 0.0f;
            fps = 0.0f;
        }
    };

    // ============================================================================
    // 捕获接口
    // ============================================================================

    class ICapture {
    public:
        virtual ~ICapture() = default;

        // ====================================================================
        // 生命周期
        // ====================================================================

        // 初始化（分配资源，不启动捕获线程）
        virtual bool initialize(const CaptureConfig& config) = 0;

        // 启动捕获（开始接收帧）
        virtual bool start() = 0;

        // 停止捕获
        virtual void stop() = 0;

        // 关闭并释放资源
        virtual void shutdown() = 0;

        // ====================================================================
        // 帧获取
        // ====================================================================

        // 同步单帧获取（阻塞，超时返回 false）
        virtual bool grabFrame(GpuTexture& outFrame,
            uint32_t timeoutMs = 16) = 0;

        // 异步回调
        virtual void setFrameCallback(FrameCapturedCallback cb) = 0;
        virtual void setErrorCallback(CaptureErrorCallback cb) = 0;
        virtual void setStateCallback(CaptureStateCallback cb) = 0;

        // ====================================================================
        // 查询
        // ====================================================================

        virtual bool isRunning() const = 0;
        virtual bool isInitialized() const = 0;
        virtual Size frameSize() const = 0;
        virtual Rect targetRect() const = 0;
        virtual CaptureBackend backend() const = 0;
        virtual const char* backendName() const = 0;
        virtual const CaptureStats& stats() const = 0;

        // ====================================================================
        // 状态检测
        // ====================================================================

        virtual bool isTargetValid() const = 0;
        virtual bool isTargetMinimized() const = 0;
        virtual bool needsReinit() const = 0;

        // ====================================================================
        // 配置
        // ====================================================================

        virtual void setMaxFps(uint32_t fps) = 0;
        virtual void setCaptureCursor(bool enabled) = 0;
    };

    // ============================================================================
    // 工厂函数
    // ============================================================================

    std::unique_ptr<ICapture> createWGCCapture();
    std::unique_ptr<ICapture> createDXGICapture();
    std::unique_ptr<ICapture> createHookedCapture();

} // namespace Lingjing