#ifdef _WIN32

#include "capture/DXGICapture.h"
#include "core/Logger.h"
#include "capture/CursorRenderer.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>

using namespace Microsoft::WRL;

namespace Lingjing {

    // ============================================================================
    // DXGI 内部实现
    // ============================================================================

    struct DXGICapture::Impl {
        // D3D11 设备
        ComPtr<ID3D11Device> d3dDevice;
        ComPtr<ID3D11DeviceContext> d3dContext;

        // DXGI
        ComPtr<IDXGIOutputDuplication> duplication;
        ComPtr<ID3D11Texture2D> stagingTexture;
        ComPtr<ID3D11Texture2D> acquiredTexture;

        // 捕获线程
        std::thread captureThread;
        std::atomic<bool> running{ false };
        std::atomic<bool> initialized{ false };
        std::atomic<bool> targetValid{ true };
        std::atomic<bool> needsReinit{ false };
        std::atomic<bool> stopping{ false };

        // 帧同步
        std::mutex frameMutex;
        std::condition_variable frameCv;
        GpuTexture latestFrame;
        bool hasNewFrame = false;
        uint64_t latestFrameId = 0;

        // 目标信息
        Size frameSize;
        Rect targetRect;
        uint32_t outputIndex = 0;

        // 配置
        CaptureConfig config;

        // 回调
        FrameCapturedCallback frameCallback;
        CaptureErrorCallback errorCallback;
        CaptureStateCallback stateCallback;

        // 统计
        CaptureStats stats;
        std::chrono::steady_clock::time_point lastFrameTime;
        std::deque<float> fpsWindow;

        // 帧率限制
        std::chrono::steady_clock::time_point lastEmitTime;
        uint32_t maxFps = 0;

        // 光标
        struct CursorState {
            bool visible = false;
            int32_t x = 0;
            int32_t y = 0;
            int32_t hotX = 0;
            int32_t hotY = 0;
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11ShaderResourceView> srv;
            bool textureValid = false;
        };
        CursorState cursor;
        std::unique_ptr<CursorRenderer> cursorRenderer;

        // 辅助方法
        bool createD3D11Device();
        bool createDuplication();
        bool handleFrame(DXGI_OUTDUPL_FRAME_INFO& frameInfo,
            IDXGIResource* resource);
        bool handleCursorUpdate(DXGI_OUTDUPL_FRAME_INFO& frameInfo);
        bool handleCursorShapeUpdate(IDXGIResource* resource);
        void captureLoop();
        void updateFps();
        bool shouldEmitFrame();
        void notifyError(ErrorCode code, const std::string& msg);
        bool reinitialize();
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    DXGICapture::DXGICapture()
        : impl_(std::make_unique<Impl>()) {
    }

    DXGICapture::~DXGICapture() {
        shutdown();
    }

    // ============================================================================
    // D3D11 设备创建
    // ============================================================================

    bool DXGICapture::Impl::createD3D11Device() {
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            d3dDevice.GetAddressOf(),
            &featureLevel,
            d3dContext.GetAddressOf());

        if (FAILED(hr)) {
            LOG_ERROR("DXGI: D3D11CreateDevice failed: 0x%08X", hr);
            return false;
        }

        LOG_INFO("DXGI: D3D11 device created (feature level 0x%X)",
            static_cast<uint32_t>(featureLevel));
        return true;
    }

    // ============================================================================
    // 创建桌面复制
    // ============================================================================

    bool DXGICapture::Impl::createDuplication() {
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) {
            LOG_ERROR("DXGI: Get IDXGIDevice failed: 0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("DXGI: GetAdapter failed: 0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(outputIndex, output.GetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("DXGI: EnumOutputs(%u) failed: 0x%08X",
                outputIndex, hr);
            return false;
        }

        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) {
            LOG_ERROR("DXGI: Get IDXGIOutput1 failed: 0x%08X", hr);
            return false;
        }

        hr = output1->DuplicateOutput(
            d3dDevice.Get(),
            duplication.GetAddressOf());

        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                LOG_ERROR("DXGI: DuplicateOutput not available "
                    "(another application may be using it)");
            }
            else {
                LOG_ERROR("DXGI: DuplicateOutput failed: 0x%08X", hr);
            }
            return false;
        }

        // 获取输出描述
        DXGI_OUTDUPL_DESC desc;
        duplication->GetDesc(&desc);
        frameSize.width = desc.ModeDesc.Width;
        frameSize.height = desc.ModeDesc.Height;

        // 创建 staging 纹理（用于非零拷贝路径）
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = desc.ModeDesc.Width;
        texDesc.Height = desc.ModeDesc.Height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = desc.ModeDesc.Format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_STAGING;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = d3dDevice->CreateTexture2D(&texDesc, nullptr,
            stagingTexture.GetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("DXGI: CreateTexture2D (staging) failed: 0x%08X", hr);
            return false;
        }

        LOG_INFO("DXGI: duplication created %ux%u, output %u",
            frameSize.width, frameSize.height, outputIndex);

        return true;
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool DXGICapture::initialize(const CaptureConfig& config) {
        std::lock_guard<std::mutex> lock(impl_->frameMutex);

        if (impl_->initialized) {
            LOG_WARN("DXGICapture already initialized");
            return true;
        }

        impl_->config = config;
        impl_->outputIndex = config.targetOutputIndex;
        impl_->maxFps = config.maxFps;

        if (!impl_->createD3D11Device()) {
            impl_->notifyError(ErrorCode::D3DError, "D3D11 device creation failed");
            return false;
        }

        if (!impl_->createDuplication()) {
            impl_->notifyError(ErrorCode::CaptureInitFailed,
                "Desktop Duplication creation failed");
            return false;
        }

        if (config.captureCursor) {
            impl_->cursorRenderer = std::make_unique<CursorRenderer>();
            impl_->cursorRenderer->initialize(impl_->d3dDevice.Get(),
                impl_->d3dContext.Get());
        }

        impl_->initialized = true;
        return true;
    }

    // ============================================================================
    // 捕获线程
    // ============================================================================

    void DXGICapture::Impl::captureLoop() {
        LOG_INFO("DXGI capture thread started");

        while (!stopping.load()) {
            DXGI_OUTDUPL_FRAME_INFO frameInfo;
            ComPtr<IDXGIResource> desktopResource;

            HRESULT hr = duplication->AcquireNextFrame(
                100,
                &frameInfo,
                desktopResource.GetAddressOf());

            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }

            if (hr == DXGI_ERROR_ACCESS_LOST) {
                LOG_WARN("DXGI: access lost, attempting reinit");
                needsReinit = true;

                if (reinitialize()) {
                    stats.reinitCount++;
                    continue;
                }
                else {
                    notifyError(ErrorCode::CaptureLost,
                        "DXGI access lost and reinit failed");
                    break;
                }
            }

            if (FAILED(hr)) {
                LOG_ERROR("DXGI: AcquireNextFrame failed: 0x%08X", hr);
                continue;
            }

            // 处理光标
            if (config.captureCursor) {
                if (frameInfo.PointerShapeBufferSize > 0) {
                    handleCursorShapeUpdate(desktopResource.Get());
                }
                handleCursorUpdate(frameInfo);
            }

            // 处理帧
            if (frameInfo.LastPresentTime.QuadPart != 0) {
                handleFrame(frameInfo, desktopResource.Get());
            }

            duplication->ReleaseFrame();

            // 如果配置了最大帧率，延迟
            if (maxFps > 0) {
                auto now = std::chrono::steady_clock::now();
                float elapsedMs = std::chrono::duration<float, std::milli>(
                    now - lastEmitTime).count();
                float minInterval = 1000.0f / static_cast<float>(maxFps);

                if (elapsedMs < minInterval) {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(
                            static_cast<int>((minInterval - elapsedMs) * 1000.0f)));
                }
            }
        }

        LOG_INFO("DXGI capture thread exited");
    }

    // ============================================================================
    // 帧处理
    // ============================================================================

    bool DXGICapture::Impl::handleFrame(DXGI_OUTDUPL_FRAME_INFO& frameInfo,
        IDXGIResource* resource)
    {
        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = resource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(texture.GetAddressOf()));

        if (FAILED(hr) || !texture) {
            LOG_WARN("DXGI: failed to get ID3D11Texture2D");
            return false;
        }

        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        // 尺寸变化
        bool sizeChanged = (desc.Width != frameSize.width ||
            desc.Height != frameSize.height);

        if (sizeChanged) {
            LOG_INFO("DXGI: frame size changed: %ux%u -> %ux%u",
                frameSize.width, frameSize.height,
                desc.Width, desc.Height);

            frameSize.width = desc.Width;
            frameSize.height = desc.Height;

            // 重建 staging
            stagingTexture.Reset();
            D3D11_TEXTURE2D_DESC texDesc = desc;
            texDesc.Usage = D3D11_USAGE_STAGING;
            texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            texDesc.BindFlags = 0;
            texDesc.MiscFlags = 0;

            d3dDevice->CreateTexture2D(&texDesc, nullptr,
                stagingTexture.GetAddressOf());
        }

        // 更新统计
        auto now = std::chrono::steady_clock::now();
        if (lastFrameTime.time_since_epoch().count() > 0) {
            float deltaMs = std::chrono::duration<float, std::milli>(
                now - lastFrameTime).count();
            stats.lastCaptureTimeMs = deltaMs;
            if (deltaMs > stats.maxCaptureTimeMs) {
                stats.maxCaptureTimeMs = deltaMs;
            }
            float alpha = 0.1f;
            stats.avgCaptureTimeMs = stats.avgCaptureTimeMs * (1.0f - alpha)
                + deltaMs * alpha;
        }
        lastFrameTime = now;
        lastEmitTime = now;

        stats.capturedFrames++;
        stats.totalBytesCaptured +=
            static_cast<uint64_t>(desc.Width) * desc.Height * 4;

        // 光标渲染到纹理
        if (cursorRenderer && config.captureCursor && cursor.visible) {
            cursorRenderer->render(texture.Get(),
                cursor.x, cursor.y,
                cursor.texture.Get());
        }

        // 构造 GpuTexture
        GpuTexture gpuTex;
        gpuTex.nativeHandle = texture.Get();
        gpuTex.width = desc.Width;
        gpuTex.height = desc.Height;
        gpuTex.rowPitch = desc.Width * 4;
        gpuTex.format = TextureFormat::B8G8R8A8_UNORM;
        gpuTex.frameIndex = ++latestFrameId;
        gpuTex.timestampNs = nowNs();

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = gpuTex;
            hasNewFrame = true;
        }

        frameCv.notify_one();

        updateFps();

        if (frameCallback) {
            frameCallback(gpuTex);
        }

        return true;
    }

    // ============================================================================
    // 光标处理
    // ============================================================================

    bool DXGICapture::Impl::handleCursorUpdate(
        DXGI_OUTDUPL_FRAME_INFO& frameInfo)
    {
        if (frameInfo.LastMouseUpdateTime.QuadPart == 0) {
            return false;
        }

        cursor.visible = frameInfo.PointerPosition.Visible;
        cursor.x = frameInfo.PointerPosition.Position.x;
        cursor.y = frameInfo.PointerPosition.Position.y;

        return true;
    }

    bool DXGICapture::Impl::handleCursorShapeUpdate(IDXGIResource* resource) {
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo;

        // 第一次调用获取大小
        UINT bufferSize = 0;
        HRESULT hr = duplication->GetFramePointerShape(
            0, nullptr, &bufferSize, &shapeInfo);

        if (hr != DXGI_ERROR_MORE_DATA && FAILED(hr)) {
            return false;
        }

        std::vector<uint8_t> buffer(bufferSize);
        hr = duplication->GetFramePointerShape(
            bufferSize, buffer.data(), &bufferSize, &shapeInfo);

        if (FAILED(hr)) {
            return false;
        }

        cursor.hotX = shapeInfo.HotSpot.x;
        cursor.hotY = shapeInfo.HotSpot.y;

        // 上传到纹理
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = shapeInfo.Width;
        texDesc.Height = shapeInfo.Height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = buffer.data();
        initData.SysMemPitch = shapeInfo.Pitch;

        cursor.texture.Reset();
        hr = d3dDevice->CreateTexture2D(&texDesc, &initData,
            cursor.texture.GetAddressOf());

        if (SUCCEEDED(hr)) {
            cursor.textureValid = true;
        }

        return SUCCEEDED(hr);
    }

    // ============================================================================
    // 重初始化
    // ============================================================================

    bool DXGICapture::Impl::reinitialize() {
        LOG_INFO("DXGI: attempting reinitialization");

        duplication.Reset();
        stagingTexture.Reset();

        // 延迟
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config.reinitDelayMs));

        if (!createDuplication()) {
            LOG_ERROR("DXGI: reinit failed");
            return false;
        }

        LOG_INFO("DXGI: reinit succeeded");
        needsReinit = false;

        return true;
    }

    // ============================================================================
    // 帧率限制
    // ============================================================================

    bool DXGICapture::Impl::shouldEmitFrame() {
        if (maxFps == 0) return true;

        auto now = std::chrono::steady_clock::now();
        if (lastEmitTime.time_since_epoch().count() == 0) return true;

        float elapsedMs = std::chrono::duration<float, std::milli>(
            now - lastEmitTime).count();

        float minInterval = 1000.0f / static_cast<float>(maxFps);

        return elapsedMs >= minInterval;
    }

    void DXGICapture::Impl::updateFps() {
        auto now = std::chrono::steady_clock::now();

        if (lastFrameTime.time_since_epoch().count() > 0) {
            float deltaMs = std::chrono::duration<float, std::milli>(
                now - lastFrameTime).count();

            if (deltaMs > 0.0f) {
                fpsWindow.push_back(deltaMs);
                if (fpsWindow.size() > 60) {
                    fpsWindow.pop_front();
                }

                if (!fpsWindow.empty()) {
                    float sum = 0.0f;
                    for (float d : fpsWindow) sum += d;
                    float avg = sum / static_cast<float>(fpsWindow.size());
                    stats.fps = (avg > 0.0f) ? (1000.0f / avg) : 0.0f;
                }
            }
        }
    }

    // ============================================================================
    // 错误通知
    // ============================================================================

    void DXGICapture::Impl::notifyError(ErrorCode code, const std::string& msg) {
        if (errorCallback) {
            Error e(code, msg);
            errorCallback(e);
        }
    }

    // ============================================================================
    // 启动/停止
    // ============================================================================

    bool DXGICapture::start() {
        if (!impl_->initialized) {
            LOG_ERROR("DXGICapture not initialized");
            return false;
        }

        if (impl_->running.exchange(true)) {
            return true;
        }

        impl_->stopping = false;
        impl_->captureThread = std::thread(
            [this] { impl_->captureLoop(); });

        if (impl_->stateCallback) {
            impl_->stateCallback(true);
        }

        LOG_INFO("DXGICapture started");
        return true;
    }

    void DXGICapture::stop() {
        if (!impl_->running.exchange(false)) return;

        impl_->stopping = true;

        if (impl_->captureThread.joinable()) {
            impl_->captureThread.join();
        }

        if (impl_->stateCallback) {
            impl_->stateCallback(false);
        }

        LOG_INFO("DXGICapture stopped");
    }

    // ============================================================================
    // 帧获取
    // ============================================================================

    bool DXGICapture::grabFrame(GpuTexture& outFrame, uint32_t timeoutMs) {
        std::unique_lock<std::mutex> lock(impl_->frameMutex);

        bool got = impl_->frameCv.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this] { return impl_->hasNewFrame; });

        if (!got) return false;

        outFrame = impl_->latestFrame;
        impl_->hasNewFrame = false;

        return true;
    }

    // ============================================================================
    // 回调
    // ============================================================================

    void DXGICapture::setFrameCallback(FrameCapturedCallback cb) {
        impl_->frameCallback = std::move(cb);
    }

    void DXGICapture::setErrorCallback(CaptureErrorCallback cb) {
        impl_->errorCallback = std::move(cb);
    }

    void DXGICapture::setStateCallback(CaptureStateCallback cb) {
        impl_->stateCallback = std::move(cb);
    }

    // ============================================================================
    // 查询
    // ============================================================================

    bool DXGICapture::isRunning() const { return impl_->running.load(); }
    bool DXGICapture::isInitialized() const { return impl_->initialized.load(); }
    Size DXGICapture::frameSize() const { return impl_->frameSize; }

    Rect DXGICapture::targetRect() const {
        Rect r;
        r.x = 0;
        r.y = 0;
        r.width = static_cast<int32_t>(impl_->frameSize.width);
        r.height = static_cast<int32_t>(impl_->frameSize.height);
        return r;
    }

    CaptureBackend DXGICapture::backend() const {
        return CaptureBackend::DXGI_DD;
    }

    const char* DXGICapture::backendName() const {
        return "DXGI Desktop Duplication";
    }

    const CaptureStats& DXGICapture::stats() const { return impl_->stats; }

    bool DXGICapture::isTargetValid() const {
        return impl_->duplication != nullptr;
    }

    bool DXGICapture::isTargetMinimized() const {
        return false;  // DXGI 全屏模式不检测最小化
    }

    bool DXGICapture::needsReinit() const {
        return impl_->needsReinit.load();
    }

    void DXGICapture::setMaxFps(uint32_t fps) {
        impl_->maxFps = fps;
    }

    void DXGICapture::setCaptureCursor(bool enabled) {
        impl_->config.captureCursor = enabled;
    }

    // ============================================================================
    // 关闭
    // ============================================================================

    void DXGICapture::shutdown() {
        if (!impl_) return;

        stop();

        if (impl_->cursorRenderer) {
            impl_->cursorRenderer->shutdown();
            impl_->cursorRenderer.reset();
        }

        impl_->cursor.texture.Reset();
        impl_->cursor.srv.Reset();
        impl_->stagingTexture.Reset();
        impl_->duplication.Reset();
        impl_->d3dContext.Reset();
        impl_->d3dDevice.Reset();

        impl_->initialized = false;

        LOG_INFO("DXGICapture shutdown");
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<ICapture> createDXGICapture() {
        return std::make_unique<DXGICapture>();
    }

} // namespace Lingjing
#endif