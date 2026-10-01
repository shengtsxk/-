#ifdef _WIN32

#include "capture/HookedCapture.h"
#include "core/Logger.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <unordered_set>

using namespace Microsoft::WRL;

namespace Lingjing {

    // ============================================================================
    // 已知反作弊模块列表
    // ============================================================================

    static const wchar_t* kKnownAntiCheatModules[] = {
        L"EasyAntiCheat.dll",
        L"EasyAntiCheat_EOS.dll",
        L"BEClient.dll",
        L"BEDaisy.sys",
        L"BEService.exe",
        L"vgc.dll",
        L"vgtray.exe",
        L"vgk.sys",
        L"Ricochet.sys",
        L"PnkBstrA.exe",
        L"PnkBstrB.exe",
        L"FairFight.dll",
        L"nProtect GameGuard",
        L"XignCode3",
        L"ACE-BASE.sys",
        L"TenProtect.dll",
        L"TenSafe.dll",
        L"GameMon.des",
        L"npggNT.des",
        L"EQNLaunch.exe",
    };

    static constexpr size_t kKnownAntiCheatCount =
        sizeof(kKnownAntiCheatModules) / sizeof(kKnownAntiCheatModules[0]);

    // ============================================================================
    // Hooked 内部实现
    // ============================================================================

    struct HookedCapture::Impl {
        // 状态
        std::atomic<bool> running{ false };
        std::atomic<bool> initialized{ false };
        std::atomic<bool> antiCheatDetected{ false };

        // 目标
        void* targetHwnd = nullptr;
        uint32_t targetPid = 0;

        // 帧同步
        std::mutex frameMutex;
        std::condition_variable frameCv;
        GpuTexture latestFrame;
        bool hasNewFrame = false;
        uint64_t latestFrameId = 0;

        // 尺寸
        Size frameSize;
        Rect targetRect;

        // 配置
        CaptureConfig config;

        // 回调
        FrameCapturedCallback frameCallback;
        CaptureErrorCallback errorCallback;
        CaptureStateCallback stateCallback;

        // 统计
        CaptureStats stats;

        // 帧率
        uint32_t maxFps = 0;
        std::chrono::steady_clock::time_point lastEmitTime;

        // 注入句柄
        HANDLE hProcess = nullptr;
        HMODULE hInjectedModule = nullptr;
        void* sharedMemory = nullptr;
        HANDLE hSharedMemory = nullptr;
        HANDLE hFrameEvent = nullptr;
        std::thread monitorThread;
        std::atomic<bool> stopping{ false };

        // 辅助方法
        bool detectAntiCheat();
        bool injectHookDll();
        bool setupSharedMemory();
        void monitorLoop();
        void processSharedFrame();
        void notifyError(ErrorCode code, const std::string& msg);
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    HookedCapture::HookedCapture()
        : impl_(std::make_unique<Impl>()) {
    }

    HookedCapture::~HookedCapture() {
        shutdown();
    }

    // ============================================================================
    // 反作弊检测
    // ============================================================================

    bool HookedCapture::Impl::detectAntiCheat() {
        if (!targetHwnd) return false;

        DWORD pid = 0;
        GetWindowThreadProcessId(static_cast<HWND>(targetHwnd), &pid);
        targetPid = pid;

        HANDLE hProc = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE, pid);

        if (!hProc) {
            LOG_WARN("HookedCapture: cannot open process for anti-cheat check");
            return false;
        }

        bool detected = false;

        // 枚举模块
        HMODULE modules[1024];
        DWORD needed;
        if (EnumProcessModules(hProc, modules, sizeof(modules), &needed)) {
            DWORD count = needed / sizeof(HMODULE);
            for (DWORD i = 0; i < count; ++i) {
                wchar_t name[MAX_PATH];
                if (GetModuleBaseNameW(hProc, modules[i], name, MAX_PATH)) {
                    for (size_t j = 0; j < kKnownAntiCheatCount; ++j) {
                        if (_wcsicmp(name, kKnownAntiCheatModules[j]) == 0) {
                            LOG_WARN("HookedCapture: anti-cheat detected: %ls",
                                kKnownAntiCheatModules[j]);
                            detected = true;
                            break;
                        }
                    }
                    if (detected) break;
                }
            }
        }

        CloseHandle(hProc);
        return detected;
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool HookedCapture::initialize(const CaptureConfig& config) {
        if (impl_->initialized) return true;

        impl_->config = config;
        impl_->targetHwnd = config.targetHwnd;
        impl_->maxFps = config.maxFps;

        if (!impl_->targetHwnd || !IsWindow(static_cast<HWND>(impl_->targetHwnd))) {
            LOG_ERROR("HookedCapture: invalid HWND");
            return false;
        }

        // 检测反作弊
        if (impl_->detectAntiCheat()) {
            impl_->antiCheatDetected = true;
            LOG_ERROR("HookedCapture: anti-cheat detected, refusing to hook");
            impl_->notifyError(ErrorCode::CaptureInitFailed,
                "Anti-cheat detected, refusing to hook");
            return false;
        }

        // 获取窗口尺寸
        RECT rect;
        GetClientRect(static_cast<HWND>(impl_->targetHwnd), &rect);
        impl_->frameSize.width = rect.right - rect.left;
        impl_->frameSize.height = rect.bottom - rect.top;

        impl_->targetRect.x = 0;
        impl_->targetRect.y = 0;
        impl_->targetRect.width = static_cast<int32_t>(impl_->frameSize.width);
        impl_->targetRect.height = static_cast<int32_t>(impl_->frameSize.height);

        // 注入 Hook DLL
        if (!impl_->injectHookDll()) {
            LOG_ERROR("HookedCapture: DLL injection failed");
            return false;
        }

        // 设置共享内存
        if (!impl_->setupSharedMemory()) {
            LOG_ERROR("HookedCapture: shared memory setup failed");
            return false;
        }

        impl_->initialized = true;
        LOG_INFO("HookedCapture initialized: %ux%u",
            impl_->frameSize.width, impl_->frameSize.height);

        return true;
    }

    // ============================================================================
    // DLL 注入
    // ============================================================================

    bool HookedCapture::Impl::injectHookDll() {
        // 获取目标进程
        DWORD pid = 0;
        GetWindowThreadProcessId(static_cast<HWND>(targetHwnd), &pid);

        hProcess = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, pid);

        if (!hProcess) {
            LOG_ERROR("HookedCapture: OpenProcess failed: %u", GetLastError());
            return false;
        }

        // 获取 DLL 路径（同目录下的 LingjingHook.dll）
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(nullptr, dllPath, MAX_PATH);

        // 替换文件名
        std::wstring dllW(dllPath);
        size_t pos = dllW.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            dllW = dllW.substr(0, pos + 1) + L"LingjingHook.dll";
        }

        // 检查 DLL 存在
        if (GetFileAttributesW(dllW.c_str()) == INVALID_FILE_ATTRIBUTES) {
            LOG_ERROR("HookedCapture: LingjingHook.dll not found at %ls",
                dllW.c_str());
            return false;
        }

        // 在目标进程分配内存
        size_t dllPathSize = (dllW.size() + 1) * sizeof(wchar_t);
        void* remoteMem = VirtualAllocEx(
            hProcess, nullptr, dllPathSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);

        if (!remoteMem) {
            LOG_ERROR("HookedCapture: VirtualAllocEx failed: %u", GetLastError());
            return false;
        }

        // 写入 DLL 路径
        if (!WriteProcessMemory(hProcess, remoteMem, dllW.c_str(),
            dllPathSize, nullptr)) {
            LOG_ERROR("HookedCapture: WriteProcessMemory failed: %u",
                GetLastError());
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return false;
        }

        // 获取 LoadLibraryW 地址
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC pLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");

        if (!pLoadLibraryW) {
            LOG_ERROR("HookedCapture: GetProcAddress LoadLibraryW failed");
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return false;
        }

        // 创建远程线程
        HANDLE hThread = CreateRemoteThread(
            hProcess, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLibraryW),
            remoteMem, 0, nullptr);

        if (!hThread) {
            LOG_ERROR("HookedCapture: CreateRemoteThread failed: %u",
                GetLastError());
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return false;
        }

        // 等待线程完成
        WaitForSingleObject(hThread, 5000);
        CloseHandle(hThread);

        // 释放远程内存
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

        LOG_INFO("HookedCapture: DLL injected successfully");
        return true;
    }

    // ============================================================================
    // 共享内存设置
    // ============================================================================

    bool HookedCapture::Impl::setupSharedMemory() {
        // 共享内存名称基于进程 ID
        wchar_t sharedName[64];
        swprintf_s(sharedName, L"LingjingShared_%u", targetPid);

        // 创建共享内存（1 MB）
        const size_t sharedSize = 1024 * 1024;

        hSharedMemory = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(sharedSize), sharedName);

        if (!hSharedMemory) {
            LOG_ERROR("HookedCapture: CreateFileMapping failed: %u",
                GetLastError());
            return false;
        }

        sharedMemory = MapViewOfFile(hSharedMemory, FILE_MAP_ALL_ACCESS,
            0, 0, sharedSize);

        if (!sharedMemory) {
            LOG_ERROR("HookedCapture: MapViewOfFile failed: %u",
                GetLastError());
            CloseHandle(hSharedMemory);
            hSharedMemory = nullptr;
            return false;
        }

        // 创建帧信号事件
        wchar_t eventName[64];
        swprintf_s(eventName, L"LingjingFrameEvent_%u", targetPid);

        hFrameEvent = CreateEventW(nullptr, FALSE, FALSE, eventName);

        if (!hFrameEvent) {
            LOG_ERROR("HookedCapture: CreateEvent failed: %u", GetLastError());
            UnmapViewOfFile(sharedMemory);
            sharedMemory = nullptr;
            CloseHandle(hSharedMemory);
            hSharedMemory = nullptr;
            return false;
        }

        return true;
    }

    // ============================================================================
    // 监控线程
    // ============================================================================

    void HookedCapture::Impl::monitorLoop() {
        LOG_INFO("HookedCapture monitor thread started");

        while (!stopping.load()) {
            // 等待帧事件
            DWORD waitResult = WaitForSingleObject(hFrameEvent, 100);

            if (waitResult == WAIT_TIMEOUT) {
                continue;
            }

            if (waitResult != WAIT_OBJECT_0) {
                LOG_WARN("HookedCapture: WaitForSingleObject failed: %u",
                    GetLastError());
                break;
            }

            processSharedFrame();
        }

        LOG_INFO("HookedCapture monitor thread exited");
    }

    // ============================================================================
    // 处理共享帧
    // ============================================================================

    void HookedCapture::Impl::processSharedFrame() {
        // 共享内存格式：
        //   offset 0:  uint32_t width
        //   offset 4:  uint32_t height
        //   offset 8:  uint32_t format (0=BGRA8, 1=RGBA8, 2=RGBA16F)
        //   offset 12: uint32_t stride
        //   offset 16: uint64_t timestampNs
        //   offset 24: uint64_t frameIndex
        //   offset 32: 像素数据

        struct SharedHeader {
            uint32_t width;
            uint32_t height;
            uint32_t format;
            uint32_t stride;
            uint64_t timestampNs;
            uint64_t frameIndex;
        };

        SharedHeader* header = static_cast<SharedHeader*>(sharedMemory);

        if (header->width == 0 || header->height == 0) {
            return;
        }

        // 更新尺寸
        if (header->width != frameSize.width ||
            header->height != frameSize.height) {
            frameSize.width = header->width;
            frameSize.height = header->height;
            LOG_INFO("HookedCapture: frame size changed to %ux%u",
                frameSize.width, frameSize.height);
        }

        // 更新统计
        stats.capturedFrames++;
        stats.totalBytesCaptured += header->stride * header->height;

        // 构造 GpuTexture（指向共享内存）
        GpuTexture gpuTex;
        gpuTex.nativeHandle = static_cast<uint8_t*>(sharedMemory) + 32;
        gpuTex.width = header->width;
        gpuTex.height = header->height;
        gpuTex.rowPitch = header->stride;
        gpuTex.format = (header->format == 0) ?
            TextureFormat::B8G8R8A8_UNORM :
            ((header->format == 1) ?
                TextureFormat::R8G8B8A8_UNORM :
                TextureFormat::R16G16B16A16_FLOAT);
        gpuTex.frameIndex = header->frameIndex;
        gpuTex.timestampNs = header->timestampNs;

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = gpuTex;
            hasNewFrame = true;
        }

        frameCv.notify_one();

        if (frameCallback) {
            frameCallback(gpuTex);
        }
    }

    // ============================================================================
    // 错误通知
    // ============================================================================

    void HookedCapture::Impl::notifyError(ErrorCode code,
        const std::string& msg)
    {
        if (errorCallback) {
            Error e(code, msg);
            errorCallback(e);
        }
    }

    // ============================================================================
    // 启动/停止
    // ============================================================================

    bool HookedCapture::start() {
        if (!impl_->initialized) {
            LOG_ERROR("HookedCapture not initialized");
            return false;
        }

        if (impl_->running.exchange(true)) return true;

        impl_->stopping = false;
        impl_->monitorThread = std::thread(
            [this] { impl_->monitorLoop(); });

        if (impl_->stateCallback) impl_->stateCallback(true);

        LOG_INFO("HookedCapture started");
        return true;
    }

    void HookedCapture::stop() {
        if (!impl_->running.exchange(false)) return;

        impl_->stopping = true;

        if (impl_->monitorThread.joinable()) {
            impl_->monitorThread.join();
        }

        if (impl_->stateCallback) impl_->stateCallback(false);

        LOG_INFO("HookedCapture stopped");
    }

    // ============================================================================
    // 帧获取
    // ============================================================================

    bool HookedCapture::grabFrame(GpuTexture& outFrame, uint32_t timeoutMs) {
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

    void HookedCapture::setFrameCallback(FrameCapturedCallback cb) {
        impl_->frameCallback = std::move(cb);
    }

    void HookedCapture::setErrorCallback(CaptureErrorCallback cb) {
        impl_->errorCallback = std::move(cb);
    }

    void HookedCapture::setStateCallback(CaptureStateCallback cb) {
        impl_->stateCallback = std::move(cb);
    }

    // ============================================================================
    // 查询
    // ============================================================================

    bool HookedCapture::isRunning() const { return impl_->running.load(); }
    bool HookedCapture::isInitialized() const { return impl_->initialized.load(); }
    Size HookedCapture::frameSize() const { return impl_->frameSize; }
    Rect HookedCapture::targetRect() const { return impl_->targetRect; }
    CaptureBackend HookedCapture::backend() const { return CaptureBackend::Hook; }
    const char* HookedCapture::backendName() const { return "Graphics Hook"; }
    const CaptureStats& HookedCapture::stats() const { return impl_->stats; }

    bool HookedCapture::isTargetValid() const {
        if (!impl_->targetHwnd) return false;
        if (!IsWindow(static_cast<HWND>(impl_->targetHwnd))) return false;
        return true;
    }

    bool HookedCapture::isTargetMinimized() const {
        if (!impl_->targetHwnd) return false;
        return IsIconic(static_cast<HWND>(impl_->targetHwnd)) != FALSE;
    }

    bool HookedCapture::needsReinit() const { return false; }

    void HookedCapture::setMaxFps(uint32_t fps) { impl_->maxFps = fps; }
    void HookedCapture::setCaptureCursor(bool enabled) {
        impl_->config.captureCursor = enabled;
    }

    // ============================================================================
    // 安全检测
    // ============================================================================

    bool HookedCapture::isSafeForAntiCheat() const {
        return !impl_->antiCheatDetected.load();
    }

    // ============================================================================
    // 关闭
    // ============================================================================

    void HookedCapture::shutdown() {
        if (!impl_) return;

        stop();

        if (impl_->sharedMemory) {
            UnmapViewOfFile(impl_->sharedMemory);
            impl_->sharedMemory = nullptr;
        }

        if (impl_->hSharedMemory) {
            CloseHandle(impl_->hSharedMemory);
            impl_->hSharedMemory = nullptr;
        }

        if (impl_->hFrameEvent) {
            CloseHandle(impl_->hFrameEvent);
            impl_->hFrameEvent = nullptr;
        }

        if (impl_->hProcess) {
            CloseHandle(impl_->hProcess);
            impl_->hProcess = nullptr;
        }

        impl_->initialized = false;
        LOG_INFO("HookedCapture shutdown");
    }

    std::unique_ptr<ICapture> createHookedCapture() {
        return std::make_unique<HookedCapture>();
    }

} // namespace Lingjing
#endif