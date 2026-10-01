#pragma once
#ifdef _WIN32

#include "capture/ICapture.h"
#include <memory>

namespace Lingjing {

    // 图形钩子捕获通过 Hook D3D11 Present 获得原始纹理
    // 优点：最低延迟，零拷贝，可获取游戏内部分辨率纹理
    // 缺点：需要注入 DLL，反作弊风险高，本软件默认关闭

    class HookedCapture : public ICapture {
    public:
        HookedCapture();
        ~HookedCapture() override;

        bool initialize(const CaptureConfig& config) override;
        bool start() override;
        void stop() override;
        void shutdown() override;

        bool grabFrame(GpuTexture& outFrame, uint32_t timeoutMs = 16) override;
        void setFrameCallback(FrameCapturedCallback cb) override;
        void setErrorCallback(CaptureErrorCallback cb) override;
        void setStateCallback(CaptureStateCallback cb) override;

        bool isRunning() const override;
        bool isInitialized() const override;
        Size frameSize() const override;
        Rect targetRect() const override;
        CaptureBackend backend() const override;
        const char* backendName() const override;
        const CaptureStats& stats() const override;

        bool isTargetValid() const override;
        bool isTargetMinimized() const override;
        bool needsReinit() const override;

        void setMaxFps(uint32_t fps) override;
        void setCaptureCursor(bool enabled) override;

        // 安全检测
        bool isSafeForAntiCheat() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing
#endif