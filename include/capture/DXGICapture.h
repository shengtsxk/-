#pragma once
#ifdef _WIN32

#include "capture/ICapture.h"
#include <memory>

namespace Lingjing {

    class DXGICapture : public ICapture {
    public:
        DXGICapture();
        ~DXGICapture() override;

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

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing
#endif