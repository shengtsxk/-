#ifdef _WIN32

#include "capture/WGCCapture.h"
#include "core/Logger.h"
#include "capture/CursorRenderer.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>

using namespace Microsoft::WRL;
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace Lingjing {

    // ============================================================================
    // WGC 内部实现
    // ============================================================================

    struct WGCCapture::Impl {
        // D3D11 设备
        ComPtr<ID3D11Device> d3dDevice;
        ComPtr<ID3D11DeviceContext> d3dContext;
        IDirect3DDevice winrtDevice{ nullptr };

        // WGC 对象
        GraphicsCaptureItem captureItem{ nullptr };
        Direct3D11CaptureFramePool framePool{ nullptr };
        GraphicsCaptureSession session{ nullptr };

        // 帧同步
        std::mutex frameMutex;
        std::condition_variable frameCv;
        GpuTexture latestFrame;
        bool hasNewFrame = false;
        uint64_t latestFrameId = 0;
        uint64_t consumedFrameId = 0;

        // 环形缓冲
        struct RingEntry {
            GpuTexture texture;
            uint64_t frameId;
            bool ready;
        };
        std::vector<RingEntry> ringBuffer;
        // CPU 读回双缓冲（回调内同设备读回，Presenter 直接 memcpy，避免跨设备 CopyResource）
        std::mutex cpuFrameMutex;           // 保护 CPU 读回缓冲的 resize/memcpy
        std::vector<uint8_t> cpuFrameBuffers[2];
        int cpuBufferIdx = 0;
        uint32_t ringWriteIdx = 0;
        uint32_t ringReadIdx = 0;
        std::atomic<uint32_t> ringCount{ 0 };

        // 状态
        std::atomic<bool> running{ false };
        std::atomic<bool> initialized{ false };
        std::atomic<bool> targetValid{ true };
        std::atomic<bool> needsReinit{ false };

        // 目标信息
        Size frameSize;
        Rect targetRect;
        HWND hwnd = nullptr;

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

        // 事件令牌
        winrt::event_token frameArrivedToken{};

        // 光标
        std::unique_ptr<CursorRenderer> cursorRenderer;

        // 辅助方法
        bool createD3D11Device();
        bool createCaptureItem();
        bool createFramePool();
        bool createSession();
        bool reinitialize();
        void onFrameArrived(Direct3D11CaptureFramePool const& sender,
            winrt::Windows::Foundation::IInspectable const&);
        void processFrameFromPool(Direct3D11CaptureFramePool const& pool);
        void pushToRingBuffer(const GpuTexture& texture);
        void updateFps();
        bool shouldEmitFrame();
        void notifyError(ErrorCode code, const std::string& msg);
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    WGCCapture::WGCCapture()
        : impl_(std::make_unique<Impl>()) {
    }

    WGCCapture::~WGCCapture() {
        shutdown();
    }

    // ============================================================================
    // D3D11 设备创建
    // ============================================================================

    bool WGCCapture::Impl::createD3D11Device() {
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
            LOG_ERROR("D3D11CreateDevice failed: 0x%08X", hr);
            return false;
        }

        LOG_INFO("D3D11 device created, feature level: 0x%X",
            static_cast<uint32_t>(featureLevel));
        return true;
    }

    // ============================================================================
    // 创建 GraphicsCaptureItem
    // ============================================================================

    bool WGCCapture::Impl::createCaptureItem() {
        try {
            auto interopFactory = winrt::get_activation_factory<
                GraphicsCaptureItem,
                IGraphicsCaptureItemInterop>();

            GraphicsCaptureItem item{ nullptr };
            HRESULT hr = interopFactory->CreateForWindow(
                hwnd,
                winrt::guid_of<GraphicsCaptureItem>(),
                winrt::put_abi(item));

            if (FAILED(hr)) {
                LOG_ERROR("CreateForWindow failed: 0x%08X", hr);
                return false;
            }

            captureItem = item;
            return true;
        }
        catch (winrt::hresult_error const& e) {
            LOG_ERROR("GraphicsCaptureItem creation failed: %ls",
                e.message().c_str());
            return false;
        }
    }

    // ============================================================================
    // 创建帧池
    // ============================================================================

    bool WGCCapture::Impl::createFramePool() {
        try {
            ComPtr<IDXGIDevice> dxgiDevice;
            HRESULT hr = d3dDevice.As(&dxgiDevice);
            if (FAILED(hr)) {
                LOG_ERROR("Failed to get IDXGIDevice: 0x%08X", hr);
                return false;
            }

            winrt::com_ptr<::IInspectable> inspectable;
            hr = CreateDirect3D11DeviceFromDXGIDevice(
                dxgiDevice.Get(),
                inspectable.put());

            if (FAILED(hr)) {
                LOG_ERROR("CreateDirect3D11DeviceFromDXGIDevice failed: 0x%08X", hr);
                return false;
            }

            this->winrtDevice = inspectable.as<IDirect3DDevice>();
            IDirect3DDevice winrtDevice = this->winrtDevice;

            auto size = captureItem.Size();

            framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                config.ringBufferSize,
                size);

            if (!framePool) {
                LOG_ERROR("Failed to create Direct3D11CaptureFramePool");
                return false;
            }

            return true;
        }
        catch (winrt::hresult_error const& e) {
            LOG_ERROR("Frame pool creation failed: %ls", e.message().c_str());
            return false;
        }
    }
    // ============================================================================
    // 创建会话
    // ============================================================================

    bool WGCCapture::Impl::createSession() {
        try {
            session = framePool.CreateCaptureSession(captureItem);

            // 尝试禁用黄框（Windows 11 22H2+）
            try {
                session.IsBorderRequired(false);
                LOG_INFO("WGC border disabled");
            }
            catch (...) {
                LOG_WARN("IsBorderRequired not supported on this OS version");
            }

            // 尝试启用光标捕获（由我们自己渲染）
            try {
                session.IsCursorCaptureEnabled(false);
            }
            catch (...) {}

            return true;
        }
        catch (winrt::hresult_error const& e) {
            LOG_ERROR("Session creation failed: %ls", e.message().c_str());
            return false;
        }
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool WGCCapture::initialize(const CaptureConfig& config) {
        std::lock_guard<std::mutex> lock(impl_->frameMutex);

        if (impl_->initialized) {
            LOG_WARN("WGCCapture already initialized");
            return true;
        }

        impl_->config = config;
        impl_->hwnd = static_cast<HWND>(config.targetHwnd);
        impl_->maxFps = config.maxFps;

        if (!impl_->hwnd || !IsWindow(impl_->hwnd)) {
            LOG_ERROR("WGCCapture: invalid target HWND");
            impl_->notifyError(ErrorCode::WindowNotFound, "Invalid HWND");
            return false;
        }

        // 初始化 WinRT
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...) {
            // 已初始化
        }
        // 创建 D3D11 设备
        if (!impl_->createD3D11Device()) {
            impl_->notifyError(ErrorCode::D3DError, "D3D11 device creation failed");
            return false;
        }

        // 创建捕获项
        if (!impl_->createCaptureItem()) {
            impl_->notifyError(ErrorCode::CaptureInitFailed,
                "GraphicsCaptureItem creation failed");
            return false;
        }

        // 获取窗口尺寸
        auto size = impl_->captureItem.Size();
        impl_->frameSize.width = static_cast<uint32_t>(size.Width);
        impl_->frameSize.height = static_cast<uint32_t>(size.Height);

        RECT rect;
        GetClientRect(impl_->hwnd, &rect);
        impl_->targetRect.x = rect.left;
        impl_->targetRect.y = rect.top;
        impl_->targetRect.width = rect.right - rect.left;
        impl_->targetRect.height = rect.bottom - rect.top;

        // 创建帧池
        if (!impl_->createFramePool()) {
            impl_->notifyError(ErrorCode::CaptureInitFailed,
                "Frame pool creation failed");
            return false;
        }

        // 注册帧到达回调
        impl_->frameArrivedToken = impl_->framePool.FrameArrived(
            [this](Direct3D11CaptureFramePool const& sender,
                winrt::Windows::Foundation::IInspectable const&)
            {
                impl_->processFrameFromPool(sender);
            });

        // 创建会话
        if (!impl_->createSession()) {
            impl_->notifyError(ErrorCode::CaptureInitFailed,
                "Session creation failed");
            return false;
        }

        // 光标渲染器
        if (config.captureCursor) {
            impl_->cursorRenderer = std::make_unique<CursorRenderer>();
            impl_->cursorRenderer->initialize(impl_->d3dDevice.Get(),
                impl_->d3dContext.Get());
        }

        // 初始化环形缓冲
        impl_->ringBuffer.resize(config.ringBufferSize);
        for (auto& entry : impl_->ringBuffer) {
            entry.ready = false;
            entry.frameId = 0;
        }

        // 预分配 CPU 读回缓冲容量（固定 4K 上限，避免运行中尺寸变化触发
        // realloc 导致 Presenter 持有的旧 data() 指针悬垂崩溃）
        {
            const size_t frameBytes =
                (size_t)impl_->frameSize.width * impl_->frameSize.height * 4;
            const size_t capBytes =
                frameBytes > (size_t)4096 * 4096 * 4
                    ? frameBytes : (size_t)4096 * 4096 * 4;
            impl_->cpuFrameBuffers[0].reserve(capBytes);
            impl_->cpuFrameBuffers[1].reserve(capBytes);
        }

        impl_->initialized = true;
        LOG_INFO("WGCCapture initialized: %ux%u",
            impl_->frameSize.width, impl_->frameSize.height);

        return true;
    }

    // ============================================================================
    // 帧池处理
    // ============================================================================

    void WGCCapture::Impl::processFrameFromPool(
        Direct3D11CaptureFramePool const& pool)
    {
        try {
        static thread_local int wgcCbCount = 0;
        if (++wgcCbCount <= 3 || wgcCbCount % 100 == 0) {
            LOG_INFO("WGC callback #%d", wgcCbCount);
        }
        auto frame = pool.TryGetNextFrame();
        if (!frame) {
            static thread_local int wgcNullCount = 0;
            if (++wgcNullCount <= 3 || wgcNullCount % 100 == 0) {
                LOG_WARN("WGC TryGetNextFrame null #%d", wgcNullCount);
            }
            return;
        }

        auto surface = frame.Surface();
        auto access = surface.as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = access->GetInterface(IID_PPV_ARGS(&texture));

        if (FAILED(hr) || !texture) {
            LOG_WARN("Failed to get texture from surface");
            return;
        }

        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);



        // 检查尺寸变化
        bool sizeChanged = (desc.Width != frameSize.width ||
            desc.Height != frameSize.height);

        if (sizeChanged) {
            LOG_INFO("WGC frame size changed: %ux%u -> %ux%u",
                frameSize.width, frameSize.height,
                desc.Width, desc.Height);

            // 重新创建帧池
            // 尺寸变化：仅更新尺寸，避免回调线程 Recreate 崩溃风险
            try {
            framePool.Recreate(
                this->winrtDevice,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                config.ringBufferSize,
                frame.ContentSize());
            }
            catch (winrt::hresult_error const& e) {
                LOG_WARN("Recreate failed: %ls", e.message().c_str());
            }

            frameSize.width = desc.Width;
            frameSize.height = desc.Height;
        }

        // 帧率限制
        if (!shouldEmitFrame()) {
            return;
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

        // 光标渲染（先渲染再读回，保证光标出现在输出中）
        if (cursorRenderer && config.captureCursor) {
            cursorRenderer->render(texture.Get());
        }

        // 构造 GpuTexture
        GpuTexture gpuTex;

        // 回调内同设备读回 CPU（Presenter 跨设备 CopyResource 会失败，故在此统一读回）
        {
            std::lock_guard<std::mutex> lk(cpuFrameMutex);
            D3D11_TEXTURE2D_DESC stDesc = desc;
            stDesc.Usage = D3D11_USAGE_STAGING;
            stDesc.BindFlags = 0;
            stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stDesc.MiscFlags = 0;

            ComPtr<ID3D11Texture2D> stagingTex;
            HRESULT hr = d3dDevice->CreateTexture2D(&stDesc, nullptr, &stagingTex);
            if (SUCCEEDED(hr) && stagingTex) {
                d3dContext->CopyResource(stagingTex.Get(), texture.Get());
                D3D11_MAPPED_SUBRESOURCE ms = {};
                if (SUCCEEDED(d3dContext->Map(
                    stagingTex.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
                    const int bi = cpuBufferIdx;
                    cpuBufferIdx = 1 - cpuBufferIdx;
                    auto& buf = cpuFrameBuffers[bi];
                    const size_t bytes = (size_t)desc.Width * desc.Height * 4;
                    if (buf.size() < bytes) buf.resize(bytes);  // 容量已预分配 4K，不会 realloc
                    const uint8_t* srcp = static_cast<const uint8_t*>(ms.pData);
                    uint8_t* dst = buf.data();
                    const size_t rowBytes = (size_t)desc.Width * 4;
                    for (UINT y = 0; y < desc.Height; ++y) {
                        std::memcpy(dst + (size_t)y * rowBytes,
                            srcp + (size_t)y * ms.RowPitch, rowBytes);
                    }
                    d3dContext->Unmap(stagingTex.Get(), 0);
                    gpuTex.nativeHandle = buf.data();
                    gpuTex.isCpuBuffer = true;
                }
            }
        }
        if (!gpuTex.nativeHandle) {
            gpuTex.nativeHandle = texture.Get();
            gpuTex.isCpuBuffer = false;
        }

        gpuTex.width = desc.Width;
        gpuTex.height = desc.Height;
        gpuTex.rowPitch = desc.Width * 4;
        gpuTex.format = TextureFormat::B8G8R8A8_UNORM;
        gpuTex.frameIndex = ++latestFrameId;
        gpuTex.timestampNs = nowNs();

        // 放入环形缓冲
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = gpuTex;
            hasNewFrame = true;
        }

        frameCv.notify_one();

        // 提前释放 WGC 帧/表面/访问/纹理对象，避免函数尾析构阶段异常逃逸
        frame = nullptr;
        surface = nullptr;
        access = nullptr;
        texture.Reset();

        updateFps();

        if (frameCallback) {
            frameCallback(gpuTex);
        }
        }
        catch (winrt::hresult_error const& e) {
            LOG_ERROR("WGC frame callback exception: %ls", e.message().c_str());
        }
        catch (...) {
            LOG_ERROR("WGC frame callback unknown exception");
        }
    }

    // ============================================================================
    // ============================================================================

    bool WGCCapture::Impl::shouldEmitFrame() {
        if (maxFps == 0) return true;

        auto now = std::chrono::steady_clock::now();
        if (lastEmitTime.time_since_epoch().count() == 0) {
            return true;
        }

        float elapsedMs = std::chrono::duration<float, std::milli>(
            now - lastEmitTime).count();

        float minIntervalMs = 1000.0f / static_cast<float>(maxFps);

        return elapsedMs >= minIntervalMs;
    }

    // ============================================================================
    // FPS 更新
    // ============================================================================

    void WGCCapture::Impl::updateFps() {
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
                    float avgDelta = sum / static_cast<float>(fpsWindow.size());

                    stats.fps = (avgDelta > 0.0f) ?
                        (1000.0f / avgDelta) : 0.0f;
                }
            }
        }
    }

    // ============================================================================
    // 环形缓冲
    // ============================================================================

    void WGCCapture::Impl::pushToRingBuffer(const GpuTexture& texture) {
        if (ringBuffer.empty()) return;

        auto& entry = ringBuffer[ringWriteIdx];
        entry.texture = texture;
        entry.frameId = texture.frameIndex;
        entry.ready = true;

        ringWriteIdx = (ringWriteIdx + 1) % ringBuffer.size();

        uint32_t count = ringCount.load();
        if (count < ringBuffer.size()) {
            ringCount++;
        }
    }

    // ============================================================================
    // 错误通知
    // ============================================================================

    void WGCCapture::Impl::notifyError(ErrorCode code, const std::string& msg) {
        if (errorCallback) {
            Error e(code, msg);
            errorCallback(e);
        }
    }

    // ============================================================================
    // 启动/停止
    // ============================================================================

    bool WGCCapture::start() {
        if (!impl_->initialized) {
            LOG_ERROR("WGCCapture not initialized");
            return false;
        }

        if (impl_->running.exchange(true)) {
            LOG_WARN("WGCCapture already running");
            return true;
        }

        try {
            impl_->session.StartCapture();
            LOG_INFO("WGCCapture started");

            if (impl_->stateCallback) {
                impl_->stateCallback(true);
            }

            return true;
        }
        catch (winrt::hresult_error const& e) {
            (void)e;
            impl_->notifyError(ErrorCode::CaptureInitFailed,
                "StartCapture failed");
            return false;
        }
    }

    void WGCCapture::stop() {
        if (!impl_->running.exchange(false)) return;

        try {
            if (impl_->session) {
                impl_->session.Close();
            }
            // 帧池不在 stop 中关闭，留待 shutdown 注销回调后再 Close，避免已 Close 对象再注销触发 fail-fast
        }
        catch (...) {}

        if (impl_->stateCallback) {
            impl_->stateCallback(false);
        }

        LOG_INFO("WGCCapture stopped");
    }

    bool WGCCapture::grabFrame(GpuTexture& outFrame, uint32_t timeoutMs) {
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


    void WGCCapture::setFrameCallback(FrameCapturedCallback cb) {
        impl_->frameCallback = std::move(cb);
    }

    void WGCCapture::setErrorCallback(CaptureErrorCallback cb) {
        impl_->errorCallback = std::move(cb);
    }

    void WGCCapture::setStateCallback(CaptureStateCallback cb) {
        impl_->stateCallback = std::move(cb);
    }

    // ============================================================================
    // 查询
    // ============================================================================

    bool WGCCapture::isRunning() const { return impl_->running.load(); }
    bool WGCCapture::isInitialized() const { return impl_->initialized.load(); }
    Size WGCCapture::frameSize() const { return impl_->frameSize; }
    Rect WGCCapture::targetRect() const { return impl_->targetRect; }
    CaptureBackend WGCCapture::backend() const { return CaptureBackend::WGC; }
    const char* WGCCapture::backendName() const { return "Windows Graphics Capture"; }
    const CaptureStats& WGCCapture::stats() const { return impl_->stats; }

    // ============================================================================
    // 状态检测
    // ============================================================================

    bool WGCCapture::isTargetValid() const {
        if (!impl_->hwnd) return false;
        if (!IsWindow(impl_->hwnd)) return false;
        return true;
    }

    bool WGCCapture::isTargetMinimized() const {
        if (!impl_->hwnd) return false;
        return IsIconic(impl_->hwnd) != FALSE;
    }

    bool WGCCapture::needsReinit() const {
        return impl_->needsReinit.load();
    }

    // ============================================================================
    // 配置更新
    // ============================================================================

    void WGCCapture::setMaxFps(uint32_t fps) {
        impl_->maxFps = fps;
    }

    void WGCCapture::setCaptureCursor(bool enabled) {
        impl_->config.captureCursor = enabled;
    }

    // ============================================================================
    // 关闭
    // ============================================================================

    void WGCCapture::shutdown() {
        if (!impl_) return;

        stop();

        try {
            if (impl_->frameArrivedToken) {
                if (impl_->framePool) {
                    impl_->framePool.FrameArrived(impl_->frameArrivedToken);
                }
                impl_->frameArrivedToken = {};
            }
        }
        catch (...) {}

        if (impl_->cursorRenderer) {
            impl_->cursorRenderer->shutdown();
            impl_->cursorRenderer.reset();
        }

        impl_->session = nullptr;
        impl_->framePool = nullptr;
        impl_->captureItem = nullptr;
        impl_->d3dContext.Reset();
        impl_->d3dDevice.Reset();

        impl_->initialized = false;

        LOG_INFO("WGCCapture shutdown");
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<ICapture> createWGCCapture() {
        return std::make_unique<WGCCapture>();
    }

} // namespace Lingjing
#endif
