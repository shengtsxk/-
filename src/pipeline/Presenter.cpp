#include "pipeline/Presenter.h"
#include "core/Logger.h"
#include "core/Timer.h"

#include <chrono>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    Presenter::Presenter() = default;
    Presenter::~Presenter() { shutdown(); }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool Presenter::initialize(void* targetHwnd, const PresentConfig& config) {
        if (initialized_) return true;

        if (!targetHwnd) {
            LOG_ERROR("Presenter: null target window");
            return false;
        }

        targetHwnd_ = targetHwnd;
        config_ = config;

        // 超分辨率（ED-ASR）配置
        superResEnabled_ = config.superResEnabled;
        superResScale_ = (config.superResScale >= 2 && config.superResScale <= 5)
            ? config.superResScale : 2;
        if (superResEnabled_) {
            if (superRes_.initialize()) {
                LOG_INFO("Presenter: ED-ASR super-resolution ready (scale=%u)",
                    superResScale_);
            } else {
                superResEnabled_ = false;
                LOG_WARN("Presenter: ED-ASR init failed, super-resolution disabled");
            }
        }

#ifdef _WIN32
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        // 注册输出窗口类（进程内仅一次）
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = [](HWND h, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
                switch (msg) {
                case WM_PAINT: {
                    PAINTSTRUCT ps;
                    BeginPaint(h, &ps);
                    EndPaint(h, &ps);
                    return 0;
                }
                case WM_ERASEBKGND:
                    return 1;  // 由 StretchDIBits 全量重绘，避免闪烁
                case WM_SIZE: {
                    // 尺寸变化时清理背景
                    InvalidateRect(h, nullptr, TRUE);
                    return 0;
                }
                default:
                    return DefWindowProcW(h, msg, wParam, lParam);
                }
            };
            wc.hInstance = hInst;
            wc.lpszClassName = L"LingjingOutputWnd";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RegisterClassExW(&wc);
            classRegistered = true;
        }

        // 依据目标窗口尺寸创建输出预览窗口（显示插帧后的画面）
        HWND hTarget = (HWND)targetHwnd;
        RECT rc = {};
        if (!GetClientRect(hTarget, &rc) || rc.right <= 0 || rc.bottom <= 0) {
            rc.right = 1280;
            rc.bottom = 720;
        }

        outW_ = static_cast<uint32_t>(rc.right);
        outH_ = static_cast<uint32_t>(rc.bottom);

        // 输出窗口尺寸：超分开启时按倍率放大，但限制在目标显示器工作区内，
        // 避免放大后超出屏幕（否则画面只剩左上角一小块可见）
        int wndW = (int)outW_;
        int wndH = (int)outH_;
        if (superResEnabled_ && superResScale_ >= 2) {
            wndW = wndW * (int)superResScale_;
            wndH = wndH * (int)superResScale_;
        }

        // 覆盖到目标窗口客户区上方（无边框贴合）
        POINT origin = {0, 0};
        ClientToScreen(hTarget, &origin);
        int posX = origin.x;
        int posY = origin.y;

        // 限制在目标显示器工作区内（超分放大后不超出屏幕，居中放置）
        {
            HMONITOR mon = MonitorFromWindow(hTarget, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                const int workW = mi.rcWork.right - mi.rcWork.left;
                const int workH = mi.rcWork.bottom - mi.rcWork.top;
                if (wndW > workW) wndW = workW;
                if (wndH > workH) wndH = workH;
                if (posX + wndW > mi.rcWork.right) {
                    posX = mi.rcWork.left + (workW - wndW) / 2;
                }
                if (posY + wndH > mi.rcWork.bottom) {
                    posY = mi.rcWork.top + (workH - wndH) / 2;
                }
                if (posX < mi.rcWork.left) posX = mi.rcWork.left;
                if (posY < mi.rcWork.top) posY = mi.rcWork.top;
            }
        }

        hOutputWnd_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"LingjingOutputWnd", L"灵境 输出画面",
            WS_POPUP,
            posX, posY, wndW, wndH,
            nullptr, nullptr, hInst, nullptr);

        if (hOutputWnd_) {
            ShowWindow((HWND)hOutputWnd_, SW_SHOW);
            UpdateWindow((HWND)hOutputWnd_);
            LOG_INFO("Presenter: output window created (%ux%u)", outW_, outH_);
        } else {
            LOG_WARN("Presenter: failed to create output window");
        }

        prevFrameBits_.resize((size_t)outW_ * outH_ * 4, 0);
        curFrameBits_.resize((size_t)outW_ * outH_ * 4, 0);
#endif

        initialized_ = true;

        // 初始化 GPU（D3D11）插帧混合后端；失败自动回退 CPU 混合
        initGpuBlend();

        LOG_INFO("Presenter initialized: mode=%u, queueSize=%u, output=%ux%u, gpuBlend=%d",
            static_cast<uint32_t>(config.mode),
            config.maxQueueSize,
            outW_, outH_,
            gpuBlendReady_ ? 1 : 0);

        return true;
    }

    void Presenter::shutdown() {
        if (!initialized_) return;

        clearQueue();

#ifdef _WIN32
        if (hOutputWnd_) {
            DestroyWindow((HWND)hOutputWnd_);
            hOutputWnd_ = nullptr;
        }
#endif

        prevFrameBits_.clear();
        curFrameBits_.clear();
        havePrevBits_ = false;

        shutdownGpuBlend();

        initialized_ = false;
    }

    // ============================================================================
    // 提交
    // ============================================================================

    bool Presenter::submitFrame(const GpuTexture& frame,
        TimestampNs targetTimeNs)
    {
        if (!initialized_ || !frame.valid()) return false;

        std::lock_guard<std::mutex> lock(mutex_);

        // 队列满时丢弃最旧的
        while (queue_.size() >= config_.maxQueueSize) {
            queue_.pop_front();
            stats_.droppedCount++;
        }

        FrameQueueEntry entry;
        entry.frame = frame;
        entry.targetTimestampNs = targetTimeNs;
        entry.frameId = ++nextFrameId_;

        queue_.push_back(entry);

        return true;
    }

    void Presenter::setSuperResolution(bool enabled, uint32_t scale) {
        std::lock_guard<std::mutex> lock(mutex_);
        scale = (scale >= 2 && scale <= 5) ? scale : 2;

        if (enabled && !superRes_.isReady()) {
            superRes_.initialize();  // 运行中首次开启：惰性初始化
        }

        superResEnabled_ = enabled && superRes_.isReady();
        superResScale_ = scale;

        if (superResEnabled_) {
            LOG_INFO("Presenter: super-resolution ON (scale=%u)", scale);
        } else {
            LOG_INFO("Presenter: super-resolution OFF");
        }
    }

    // ============================================================================
    // 呈现（真实输出：抓帧 -> CPU 帧混合插帧 -> 输出窗口显示）
    // ============================================================================

    bool Presenter::presentIfReady() {
        if (!initialized_) return false;
        followTargetWindow();          // 输出窗口跟随目标窗口位置/尺寸
        // 优先使用管线提交的 WGC 帧（对 GPU 渲染窗口可靠、方向正确、不黑屏）；
        // 队列为空时回退 BitBlt 抓取目标窗口
        if (!pullQueueFrame()) {
            if (!grabTargetFrame()) return false;
        }
        presentInterpolated();
        return true;
    }

    bool Presenter::presentNow(const GpuTexture& frame) {
        (void)frame;  // GPU 纹理由捕获侧持有；输出统一走 GDI 抓帧路径
        return presentIfReady();
    }

    // ============================================================================
    // 队列管理
    // ============================================================================

    void Presenter::clearQueue() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    size_t Presenter::queueSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    Presenter::Stats Presenter::stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    // ============================================================================
    // 内部辅助（Windows GDI 实现）
    // ============================================================================

    // ============================================================================
    // 取管线提交的最新帧并读回 CPU 像素（优先路径，替代 BitBlt 抓取）
    // ============================================================================

    bool Presenter::pullQueueFrame() {
        GpuTexture frame;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) return false;
            frame = queue_.back().frame; // 最新帧
            queue_.clear();              // 只保留最新，避免积压延迟
        }

        if (!frame.valid()) return false;

        uint32_t fw = frame.width;
        uint32_t fh = frame.height;

        // D3D11 纹理（WGC 捕获帧）读回（仅当 nativeHandle 是 GPU 纹理，
        // CPU 读回缓冲会走下方 memcpy 路径，避免把内存指针当纹理调用导致崩溃）
        if (!frame.isCpuBuffer && frame.nativeHandle && d3dDevice_ && d3dContext_) {
            ID3D11Texture2D* src = static_cast<ID3D11Texture2D*>(frame.nativeHandle);
            D3D11_TEXTURE2D_DESC sd = {};
            src->GetDesc(&sd);
            fw = sd.Width;
            fh = sd.Height;
            if (fw == 0 || fh == 0) return false;

            if (fw != outW_ || fh != outH_) {
                outW_ = fw; outH_ = fh;
                prevFrameBits_.resize((size_t)outW_ * outH_ * 4, 0);
                curFrameBits_.resize((size_t)outW_ * outH_ * 4, 0);
                havePrevBits_ = false;
            }

            D3D11_TEXTURE2D_DESC td = sd;
            td.Usage = D3D11_USAGE_STAGING;
            td.BindFlags = 0;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            td.MiscFlags = 0;

            ID3D11Texture2D* staging = nullptr;
            HRESULT hr = static_cast<ID3D11Device*>(d3dDevice_)->
                CreateTexture2D(&td, nullptr, &staging);
            if (FAILED(hr) || !staging) return false;

            static_cast<ID3D11DeviceContext*>(d3dContext_)->CopyResource(staging, src);

            D3D11_MAPPED_SUBRESOURCE ms = {};
            bool mapped = SUCCEEDED(static_cast<ID3D11DeviceContext*>(d3dContext_)->
                Map(staging, 0, D3D11_MAP_READ, 0, &ms));
            if (mapped) {
                const uint8_t* srcp = static_cast<const uint8_t*>(ms.pData);
                uint8_t* dst = curFrameBits_.data();
                const size_t pitch = (size_t)outW_ * 4;
                for (UINT y = 0; y < outH_; ++y) {
                    std::memcpy(dst, srcp + (size_t)y * ms.RowPitch, pitch);
                    dst += pitch;
                }
                static_cast<ID3D11DeviceContext*>(d3dContext_)->Unmap(staging, 0);

                // 若源纹理为 R8G8B8A8，与输出 BGRA 布局互换红蓝
                if (sd.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                    uint32_t* p = reinterpret_cast<uint32_t*>(curFrameBits_.data());
                    for (size_t k = 0; k < (size_t)outW_ * outH_; ++k) {
                        const uint32_t v = p[k];
                        const uint32_t r = v & 0xFFu;
                        const uint32_t b = (v >> 16) & 0xFFu;
                        p[k] = (v & 0xFF00FF00u) | b | (r << 16);
                    }
                }
            }
            staging->Release();
            return mapped;
        }

        // CPU 像素缓冲（nativeHandle 指向 BGRA 内存）
        if (frame.isCpuBuffer && frame.nativeHandle && fw > 0 && fh > 0) {
            if (fw != outW_ || fh != outH_) {
                outW_ = fw; outH_ = fh;
                prevFrameBits_.resize((size_t)outW_ * outH_ * 4, 0);
                curFrameBits_.resize((size_t)outW_ * outH_ * 4, 0);
                havePrevBits_ = false;
            }
            std::memcpy(curFrameBits_.data(), frame.nativeHandle,
                (size_t)outW_ * outH_ * 4);
            return true;
        }

        return false;
    }
    bool Presenter::grabTargetFrame() {
        HWND hwnd = (HWND)targetHwnd_;
        if (!hwnd || !IsWindow(hwnd)) return false;

        RECT rc = {};
        if (!GetClientRect(hwnd, &rc)) return false;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return false;

        // 尺寸变化时重建缓存
        if ((uint32_t)w != outW_ || (uint32_t)h != outH_) {
            outW_ = (uint32_t)w;
            outH_ = (uint32_t)h;
            prevFrameBits_.resize((size_t)w * h * 4, 0);
            curFrameBits_.resize((size_t)w * h * 4, 0);
            havePrevBits_ = false;
        }

        HDC hdcWin = GetDC(hwnd);   // 客户区 DC：与外部验证一致，抓取客户区内容
        if (!hdcWin) {
            return false;
        }

        HDC hdcMem = CreateCompatibleDC(hdcWin);

        // 用 CreateDIBSection（top-down，biHeight<0）替代 CreateCompatibleBitmap+GetDIBits：
        // DDB 位图按 bottom-up 存储，而 GetDIBits 用 -h 按 top-down 导出会垂直翻转；
        // CreateDIBSection 的内存布局与 biHeight 方向一致，BitBlt 后 y=0 即为画面顶部，方向明确正确。
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(hdcWin, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) {
            if (bmp) DeleteObject(bmp);
            DeleteDC(hdcMem);
            ReleaseDC(hwnd, hdcWin);
            return false;
        }
        HGDIOBJ oldBmp = SelectObject(hdcMem, bmp);

        // BitBlt 从窗口客户区读（快、不挂起）；PrintWindow 仅作回退（PW_RENDERFULLCONTENT 对动画窗口会等待重绘挂起）
        BOOL ok = BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
        if (!ok) {
            ok = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
        }
        if (ok) {
            // 32bpp 读回：BitBlt/PrintWindow 输出 BGRA，top-down 与 curFrameBits_ 布局一致
            std::memcpy(curFrameBits_.data(), bits, (size_t)w * h * 4);
        }

        SelectObject(hdcMem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWin);

        return ok;
    }

    void Presenter::presentInterpolated() {
        HWND hOut = (HWND)hOutputWnd_;
        if (!hOut || outW_ == 0 || outH_ == 0) return;

        size_t n = (size_t)outW_ * outH_ * 4;
        std::vector<uint8_t> blend(n);

        uint32_t m = multiplier_ > 0 ? multiplier_ : 2;

        if (havePrevBits_) {
            // 帧混合插帧：在上一帧与当前帧之间生成中间帧
            // 每调用一次推进一个插值位置（倍率 m 对应 m 个中间位置）
            frameCounter_ = (frameCounter_ + 1) % m;
            float alpha = (frameCounter_ + 1) / (float)m;
            if (frameCounter_ == m - 1) alpha = 1.0f;  // 末位置为原帧

            const uint8_t* A = prevFrameBits_.data();
            const uint8_t* B = curFrameBits_.data();
            uint8_t* O = blend.data();

            // GPU（D3D11）着色器混合优先，失败回退 CPU 定点混合
            if (!gpuBlendFrame(A, B, O, outW_, outH_, alpha)) {
                // CPU 混合：定点 alpha（0-256），按 32bit 一次混合 4 字节，减少延迟
                const int ia = (int)(alpha * 256.0f);
                const int inv = 256 - ia;
                const uint32_t* A4 = reinterpret_cast<const uint32_t*>(A);
                const uint32_t* B4 = reinterpret_cast<const uint32_t*>(B);
                uint32_t* O4 = reinterpret_cast<uint32_t*>(O);
                const size_t count = n / 4;
                for (size_t k = 0; k < count; ++k) {
                    const uint32_t a = A4[k];
                    const uint32_t b = B4[k];
                    uint32_t r = (((a & 0xFF) * inv + (b & 0xFF) * ia) >> 8);
                    uint32_t g = ((((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * ia) >> 8);
                    uint32_t bl = ((((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * ia) >> 8);
                    uint32_t al = ((((a >> 24) & 0xFF) * inv + ((b >> 24) & 0xFF) * ia) >> 8);
                    O4[k] = r | (g << 8) | (bl << 16) | (al << 24);
                }
            }
        } else {
            // 首帧直接显示
            std::memcpy(blend.data(), curFrameBits_.data(), n);
        }

        // 超分辨率：输出前放大（ED-ASR），画面按倍率放大显示
        const uint8_t* dispPtr = blend.data();
        LONG dispW = (LONG)outW_;
        LONG dispH = (LONG)outH_;
        if (superResEnabled_ && outW_ >= 4 && outH_ >= 4) {
            float srMs = 0.0f;
            // ED-ASR 只输出目标窗口区域（outW_ x outH_），
            // 内部通过 UV 映射实现 scale 倍放大，读回量小、延迟低
            presentBits_.resize((size_t)outW_ * outH_ * 4);
            if (superRes_.upscale(blend.data(), (int)outW_, (int)outH_,
                    (int)superResScale_, presentBits_, srMs)) {
                dispPtr = presentBits_.data();
                dispW = (LONG)outW_;
                dispH = (LONG)outH_;
            } else {
                // 超分运行中失败（如纹理重建失败）：关闭超分并回缩输出窗口到 1x，
                // 避免"窗口按 2 倍放大而画面仍为原尺寸"导致画面只占左上角 1/4、其余黑屏
                superResEnabled_ = false;
                LOG_WARN("Presenter: ED-ASR upscale failed, disabling super-resolution and shrinking output window");
                followTargetWindow();  // superResEnabled_ 已为 false，按 1x 重算窗口尺寸
            }
        }

        // GDI 输出到预览窗口（显示目标窗口区域的画面，放大填满窗口；
        // 超分开启时源区取超分图的目标窗口区域，而不是把整个超分图
        // 缩小回窗口——否则超分放大效果被抵消，画面只剩原大小甚至更小）
        HDC hdcOut = GetDC(hOut);
        if (hdcOut) {
            RECT client = {};
            GetClientRect(hOut, &client);
            int cw = client.right - client.left;
            int ch = client.bottom - client.top;

            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = dispW;
            bi.bmiHeader.biHeight = -dispH;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            // 源区 = 目标窗口客户区尺寸（dispPtr 数据行的前 outW_ 像素）
            StretchDIBits(hdcOut,
                0, 0, cw, ch,
                0, 0, (LONG)outW_, (LONG)outH_,
                dispPtr, &bi, DIB_RGB_COLORS, SRCCOPY);

            ReleaseDC(hOut, hdcOut);
        }

        // 更新上一帧缓存
        if (havePrevBits_) {
            std::memcpy(prevFrameBits_.data(), curFrameBits_.data(), n);
        } else {
            std::memcpy(prevFrameBits_.data(), curFrameBits_.data(), n);
            havePrevBits_ = true;
        }
    }

    // ============================================================================
    // 画面跟随窗口：输出窗口跟随目标窗口的位置与尺寸
    // ============================================================================

    void Presenter::followTargetWindow() {
#ifdef _WIN32
        HWND hTarget = (HWND)targetHwnd_;
        HWND hOut = (HWND)hOutputWnd_;
        if (!hTarget || !hOut) return;

        RECT tcr = {};
        if (!GetClientRect(hTarget, &tcr)) return;
        if (tcr.right <= 0 || tcr.bottom <= 0) return;

        // 无边框覆盖：输出窗口完全覆盖目标窗口客户区
        POINT origin = {0, 0};
        ClientToScreen(hTarget, &origin);

        const int tgtW = tcr.right - tcr.left;
        const int tgtH = tcr.bottom - tcr.top;

        // 超分辨率开启时输出窗口按倍率放大，但限制在目标显示器工作区内，
        // 避免放大后超出屏幕导致画面只剩左上角一小块
        int outW = tgtW;
        int outH = tgtH;
        if (superResEnabled_ && superResScale_ >= 2) {
            outW = tgtW * (int)superResScale_;
            outH = tgtH * (int)superResScale_;
        }

        // 限制在屏幕工作区内 + 位置钳制（超分放大后不超出屏幕）
        int newX = origin.x;
        int newY = origin.y;
        {
            HMONITOR mon = MonitorFromWindow(hTarget, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                const int workW = mi.rcWork.right - mi.rcWork.left;
                const int workH = mi.rcWork.bottom - mi.rcWork.top;
                if (outW > workW) outW = workW;
                if (outH > workH) outH = workH;
                if (newX + outW > mi.rcWork.right) newX = mi.rcWork.right - outW;
                if (newY + outH > mi.rcWork.bottom) newY = mi.rcWork.bottom - outH;
                if (newX < mi.rcWork.left) newX = mi.rcWork.left;
                if (newY < mi.rcWork.top) newY = mi.rcWork.top;
            }
        }

        RECT owr = {};
        GetWindowRect(hOut, &owr);

        if (newX != owr.left || newY != owr.top ||
            outW != (owr.right - owr.left) || outH != (owr.bottom - owr.top)) {
            MoveWindow(hOut, newX, newY, outW, outH, TRUE);
        }
#endif
    }

    // ============================================================================
    // GPU 后端：D3D11 着色器帧混合插帧
    // ============================================================================

    bool Presenter::initGpuBlend() {
        if (gpuBlendReady_) return true;

#ifdef _WIN32
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;

        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
            nullptr, 0, levels, 3, D3D11_SDK_VERSION, &dev, nullptr, &ctx);

        if (FAILED(hr)) {
            // 回退 WARP（软件 GPU）
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
                nullptr, 0, levels, 3, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        }

        if (FAILED(hr) || !dev || !ctx) {
            if (dev) dev->Release();
            if (ctx) ctx->Release();
            LOG_WARN("Presenter: D3D11 GPU backend unavailable, using CPU blend");
            return false;
        }

        d3dDevice_ = dev;
        d3dContext_ = ctx;

        D3D11_BUFFER_DESC cbd = {};
        D3D11_SAMPLER_DESC sd = {};

        // ---- 编译着色器（全屏三角形顶点 + 双纹理 alpha 混合像素） ----
        static const char* vsSrc =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };\n"
            "VSOut mainVS(uint id : SV_VertexID) {\n"
            "  VSOut o;\n"
            "  float2 uv = float2((id << 1) & 2, id & 2);\n"
            "  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
            "  o.uv = uv;\n"
            "  return o;\n"
            "}\n";

        static const char* psSrc =
            "Texture2D<float4> texA : register(t0);\n"
            "Texture2D<float4> texB : register(t1);\n"
            "SamplerState samp : register(s0);\n"
            "cbuffer Params : register(b0) { float4 alpha; };\n"
            "float4 mainPS(float2 uv : TEXCOORD) : SV_Target {\n"
            "  float4 a = texA.Sample(samp, uv);\n"
            "  float4 b = texB.Sample(samp, uv);\n"
            "  return lerp(a, b, alpha.x);\n"
            "}\n";

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errBlob = nullptr;

        hr = D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr,
            "mainVS", "vs_4_0", 0, 0, &vsBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) LOG_WARN("Presenter: VS compile: %s", (char*)errBlob->GetBufferPointer());
            goto fail;
        }
        hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, (ID3D11VertexShader**)&shaderVS_);
        if (FAILED(hr)) goto fail;

        hr = D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr,
            "mainPS", "ps_4_0", 0, 0, &psBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) LOG_WARN("Presenter: PS compile: %s", (char*)errBlob->GetBufferPointer());
            goto fail;
        }
        hr = dev->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, (ID3D11PixelShader**)&shaderPS_);
        if (FAILED(hr)) goto fail;

        // ---- 常量缓冲（alpha） ----
        cbd.ByteWidth = 16;
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = dev->CreateBuffer(&cbd, nullptr, (ID3D11Buffer**)&cbuffer_);
        if (FAILED(hr)) goto fail;

        // ---- 采样器 ----
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = dev->CreateSamplerState(&sd, (ID3D11SamplerState**)&sampler_);
        if (FAILED(hr)) goto fail;

        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();

        gpuBlendReady_ = true;
        LOG_INFO("Presenter: GPU blend backend ready (D3D11)");
        return true;

    fail:
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        if (errBlob) errBlob->Release();
        shutdownGpuBlend();
        LOG_WARN("Presenter: GPU blend backend unavailable, using CPU blend");
        return false;
#else
        return false;
#endif
    }

    void Presenter::shutdownGpuBlend() {
#ifdef _WIN32
        auto release = [](void*& p) {
            if (p) {
                reinterpret_cast<IUnknown*>(p)->Release();
                p = nullptr;
            }
        };
        release(rtvOut_);
        release(srvCur_);
        release(srvPrev_);
        release(texStaging_);
        release(texOut_);
        release(texCur_);
        release(texPrev_);
        release(cbuffer_);
        release(sampler_);
        release(shaderPS_);
        release(shaderVS_);
        release(d3dContext_);
        release(d3dDevice_);
#endif
        gpuBlendReady_ = false;
        gpuTexW_ = 0;
        gpuTexH_ = 0;
    }

    // 确保 GPU 纹理与当前帧尺寸匹配
    static bool ensureGpuTexturesImpl(void* devPtr, void*& texPrev, void*& texCur,
        void*& texOut, void*& texStaging, void*& srvPrev, void*& srvCur,
        void*& rtvOut, uint32_t& texW, uint32_t& texH,
        uint32_t w, uint32_t h)
    {
        if (texPrev && texCur && texOut && texStaging &&
            texW == w && texH == h) {
            return true;
        }

        ID3D11Device* dev = static_cast<ID3D11Device*>(devPtr);

        // 释放旧资源
        auto release = [](void*& p) {
            if (p) { reinterpret_cast<IUnknown*>(p)->Release(); p = nullptr; }
        };
        release(rtvOut);
        release(srvCur);
        release(srvPrev);
        release(texStaging);
        release(texOut);
        release(texCur);
        release(texPrev);

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texPrev);
        if (FAILED(hr)) return false;
        hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texCur);
        if (FAILED(hr)) return false;

        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texOut);
        if (FAILED(hr)) return false;

        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texStaging);
        if (FAILED(hr)) return false;

        // SRV（texPrev / texCur）
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = dev->CreateShaderResourceView((ID3D11Resource*)texPrev, &srvDesc,
            (ID3D11ShaderResourceView**)&srvPrev);
        if (FAILED(hr)) return false;
        hr = dev->CreateShaderResourceView((ID3D11Resource*)texCur, &srvDesc,
            (ID3D11ShaderResourceView**)&srvCur);
        if (FAILED(hr)) return false;

        // RTV（texOut）
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        hr = dev->CreateRenderTargetView((ID3D11Resource*)texOut, &rtvDesc,
            (ID3D11RenderTargetView**)&rtvOut);
        if (FAILED(hr)) return false;

        texW = w;
        texH = h;
        return true;
    }

    bool Presenter::gpuBlendFrame(const uint8_t* prev, const uint8_t* cur,
        uint8_t* out, uint32_t w, uint32_t h, float alpha)
    {
        if (!gpuBlendReady_ || !d3dDevice_ || !d3dContext_) return false;
        if (w == 0 || h == 0) return false;

        if (!ensureGpuTexturesImpl(d3dDevice_, texPrev_, texCur_, texOut_,
            texStaging_, srvPrev_, srvCur_, rtvOut_,
            gpuTexW_, gpuTexH_, w, h)) {
            return false;
        }

        ID3D11DeviceContext* ctx = static_cast<ID3D11DeviceContext*>(d3dContext_);
        const UINT pitch = w * 4;

        ctx->UpdateSubresource(static_cast<ID3D11Resource*>(texPrev_), 0,
            nullptr, prev, pitch, 0);
        ctx->UpdateSubresource(static_cast<ID3D11Resource*>(texCur_), 0,
            nullptr, cur, pitch, 0);

        // alpha 常量
        D3D11_MAPPED_SUBRESOURCE ms = {};
        if (SUCCEEDED(ctx->Map(static_cast<ID3D11Resource*>(cbuffer_), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            float* p = static_cast<float*>(ms.pData);
            p[0] = alpha;
            p[1] = 0.0f;
            p[2] = 0.0f;
            p[3] = 0.0f;
            ctx->Unmap(static_cast<ID3D11Resource*>(cbuffer_), 0);
        }

        ID3D11RenderTargetView* rtv = static_cast<ID3D11RenderTargetView*>(rtvOut_);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);

        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        ctx->RSSetViewports(1, &vp);

        ID3D11ShaderResourceView* srvs[2] = {
            static_cast<ID3D11ShaderResourceView*>(srvPrev_),
            static_cast<ID3D11ShaderResourceView*>(srvCur_)
        };
        ctx->PSSetShaderResources(0, 2, srvs);

        ID3D11SamplerState* samp = static_cast<ID3D11SamplerState*>(sampler_);
        ctx->PSSetSamplers(0, 1, &samp);

        ID3D11Buffer* cb = static_cast<ID3D11Buffer*>(cbuffer_);
        ctx->PSSetConstantBuffers(0, 1, &cb);

        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(static_cast<ID3D11VertexShader*>(shaderVS_), nullptr, 0);
        ctx->PSSetShader(static_cast<ID3D11PixelShader*>(shaderPS_), nullptr, 0);
        ctx->Draw(3, 0);

        // 读回
        ctx->CopyResource(static_cast<ID3D11Resource*>(texStaging_),
            static_cast<ID3D11Resource*>(texOut_));

        D3D11_MAPPED_SUBRESOURCE ms2 = {};
        if (FAILED(ctx->Map(static_cast<ID3D11Resource*>(texStaging_), 0,
            D3D11_MAP_READ, 0, &ms2))) {
            return false;
        }

        const uint8_t* src = static_cast<const uint8_t*>(ms2.pData);
        uint8_t* dst = out;
        for (UINT y = 0; y < h; ++y) {
            std::memcpy(dst, src + static_cast<size_t>(y) * ms2.RowPitch, pitch);
            dst += pitch;
        }
        ctx->Unmap(static_cast<ID3D11Resource*>(texStaging_), 0);

        return true;
    }

} // namespace Lingjing
