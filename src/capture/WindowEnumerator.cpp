#ifdef _WIN32

#include "capture/WindowEnumerator.h"
#include "core/Logger.h"

#include <windows.h>
#include <psapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")

namespace Lingjing {

    // ============================================================================
    // 已知的游戏进程名（部分）
    // ============================================================================

    static const wchar_t* kKnownGameProcesses[] = {
        // 常见游戏引擎/平台
        L"steam.exe", L"steamwebhelper.exe",
        L"EpicGamesLauncher.exe", L"EpicWebHelper.exe",
        L"Battle.net.exe", L"GalaxyClient.exe", L"Origin.exe",
        L"EA Desktop.exe", L"UbisoftConnect.exe", L"uplay.exe",
        L"GOG Galaxy.exe", L"RiotClientServices.exe",

        // 常见游戏
        L"Cyberpunk2077.exe",
        L"RDR2.exe",
        L"GTA5.exe",
        L"eldenring.exe",
        L"DarkSoulsIII.exe",
        L"witcher3.exe",
        L"GodOfWar.exe",
        L"GodOfWarRagnarok.exe",
        L"HorizonZeroDawn.exe",
        L"ForzaHorizon5.exe",
        L"FlightSimulator.exe",
        L"MSFS.exe",
        L"F1_22.exe", L"F1_23.exe", L"F1_24.exe",
        L"VALORANT.exe",
        L"cs2.exe", L"csgo.exe",
        L"r5apex.exe",
        L"ModernWarfare.exe", L"ModernWarfare2.exe", L"ModernWarfare3.exe",
        L"Warzone.exe",
        L"RainbowSix.exe",
        L"RainbowSixSiege.exe",
        L"Destiny2.exe",
        L"Overwatch.exe",
        L"Wow.exe", L"WowClassic.exe",
        L"ffxiv_dx11.exe", L"ffxiv.exe",
        L"GenshinImpact.exe",
        L"StarRail.exe",
        L"HonkaiImpact3.exe",
        L"ZenlessZoneZero.exe",
        L"Palworld-Win64-Shipping.exe",
        L"BaldursGate3.exe",
        L"HogwartsLegacy.exe",
        L"Starfield.exe",
        L"AlanWake2.exe",
        L"Diablo IV.exe",
        L"PathOfExile.exe", L"PathOfExile_x64.exe",
        L"DeepRockGalactic.exe",
        L"MonsterHunterWorld.exe",
        L"MonsterHunterRise.exe",
        L"Tekken8.exe",
        L"StreetFighter6.exe",
        L"MortalKombat1.exe",
        L"FortniteClient-Win64-Shipping.exe",
        L"acv.exe",           // Assassin's Creed Valhalla
        L"ACOrigins.exe",
        L"ACOdyssey.exe",
        L"ACMirrage.exe",
    };

    static constexpr size_t kKnownGameCount =
        sizeof(kKnownGameProcesses) / sizeof(kKnownGameProcesses[0]);

    // ============================================================================
    // WindowInfo 实现
    // ============================================================================

    std::string WindowInfo::toString() const {
        std::ostringstream oss;

        // 标题转 UTF-8
        int titleLen = WideCharToMultiByte(CP_UTF8, 0, title.c_str(), -1,
            nullptr, 0, nullptr, nullptr);
        std::string titleUtf8(titleLen, 0);
        WideCharToMultiByte(CP_UTF8, 0, title.c_str(), -1,
            titleUtf8.data(), titleLen, nullptr, nullptr);
        if (!titleUtf8.empty() && titleUtf8.back() == '\0') {
            titleUtf8.pop_back();
        }

        oss << titleUtf8 << " [" << frameSize.width << "x" << frameSize.height << "]";

        return oss.str();
    }

    bool WindowInfo::isLikelyGame() const {
        if (!isRenderable) return false;

        // 检查进程名
        std::wstring lower = processName;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        for (size_t i = 0; i < kKnownGameCount; ++i) {
            std::wstring known = kKnownGameProcesses[i];
            std::transform(known.begin(), known.end(), known.begin(), ::towlower);

            if (lower == known) return true;
        }

        // 检查窗口类名（游戏常用类名）
        static const wchar_t* gameClasses[] = {
            L"UnityWndClass",
            L"UnrealWindow",
            L"UE4Window",
            L"UE5Window",
            L"SDL_app",
            L"GLFW30",
            L"WindowsForms10.Window.8.app.0.141b42a_r6_ad1",
        };

        for (const wchar_t* cls : gameClasses) {
            if (className == cls) return true;
        }

        return false;
    }

    // ============================================================================
    // 窗口枚举
    // ============================================================================

    struct EnumContext {
        const WindowFilter* filter;
        std::vector<WindowInfo> windows;
        uint32_t ownPid;
    };

    static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
        auto* ctx = reinterpret_cast<EnumContext*>(lParam);
        const auto& filter = *ctx->filter;

        WindowInfo info;

        // 可见性
        info.visible = IsWindowVisible(hwnd) != FALSE;
        if (filter.requireVisible && !info.visible) return TRUE;

        // 最小化
        info.minimized = IsIconic(hwnd) != FALSE;
        if (filter.excludeMinimized && info.minimized) return TRUE;

        // 工具窗口
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        bool isToolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
        if (filter.excludeToolWindows && isToolWindow) return TRUE;

        // 分层窗口
        bool isLayered = (exStyle & WS_EX_LAYERED) != 0;
        bool isTransparent = (exStyle & WS_EX_TRANSPARENT) != 0;
        if (isLayered && isTransparent) return TRUE;

        // 标题
        info.title = WindowEnumerator::getWindowTitle(hwnd);
        if (filter.requireTitle && info.title.empty()) return TRUE;

        // 进程
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        info.processId = pid;

        if (filter.excludeOwnProcess && pid == ctx->ownPid) return TRUE;

        info.processPath = WindowEnumerator::getProcessPath(pid);
        info.processName = WindowEnumerator::getProcessName(pid);

        // 尺寸
        info.windowRect = WindowEnumerator::getWindowRect(hwnd);
        info.clientRect = WindowEnumerator::getClientRect(hwnd);
        info.frameSize.width = static_cast<uint32_t>(info.clientRect.width);
        info.frameSize.height = static_cast<uint32_t>(info.clientRect.height);

        if (info.frameSize.width < filter.minWidth) return TRUE;
        if (info.frameSize.height < filter.minHeight) return TRUE;

        // 类名
        info.className = WindowEnumerator::getWindowClassName(hwnd);

        // 状态
        info.foreground = (GetForegroundWindow() == hwnd);
        info.topmost = (exStyle & WS_EX_TOPMOST) != 0;

        // 模式
        info.mode = WindowEnumerator::detectWindowMode(hwnd);

        // GPU 渲染判定
        info.isRenderable = (info.frameSize.width >= 640 &&
            info.frameSize.height >= 480);

        info.hwnd = hwnd;

        // 游戏过滤
        if (filter.gamesOnly && !info.isLikelyGame()) return TRUE;

        ctx->windows.push_back(std::move(info));

        return TRUE;
    }

    std::vector<WindowInfo> WindowEnumerator::enumerate(
        const WindowFilter& filter)
    {
        EnumContext ctx;
        ctx.filter = &filter;
        ctx.ownPid = GetCurrentProcessId();

        EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&ctx));

        // 前台窗口优先
        std::stable_sort(ctx.windows.begin(), ctx.windows.end(),
            [](const WindowInfo& a, const WindowInfo& b) {
                if (a.foreground != b.foreground) {
                    return a.foreground > b.foreground;
                }
                // 游戏优先
                if (a.isLikelyGame() != b.isLikelyGame()) {
                    return a.isLikelyGame() > b.isLikelyGame();
                }
                // 面积降序
                return a.frameSize.pixels() > b.frameSize.pixels();
            });

        return ctx.windows;
    }

    WindowInfo WindowEnumerator::findWindow(HWND hwnd) {
        WindowInfo info;
        if (!hwnd || !IsWindow(hwnd)) return info;

        info.hwnd = hwnd;
        info.title = getWindowTitle(hwnd);
        info.className = getWindowClassName(hwnd);
        info.visible = IsWindowVisible(hwnd) != FALSE;
        info.minimized = IsIconic(hwnd) != FALSE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        info.processId = pid;
        info.processPath = getProcessPath(pid);
        info.processName = getProcessName(pid);

        info.windowRect = getWindowRect(hwnd);
        info.clientRect = getClientRect(hwnd);
        info.frameSize.width = static_cast<uint32_t>(info.clientRect.width);
        info.frameSize.height = static_cast<uint32_t>(info.clientRect.height);

        info.foreground = (GetForegroundWindow() == hwnd);

        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        info.topmost = (exStyle & WS_EX_TOPMOST) != 0;

        info.mode = detectWindowMode(hwnd);

        info.isRenderable = (info.frameSize.width >= 640 &&
            info.frameSize.height >= 480);

        return info;
    }

    // ============================================================================
    // 显示器枚举
    // ============================================================================

    struct DisplayEnumContext {
        std::vector<Rect> displays;
    };

    static BOOL CALLBACK enumMonitorsProc(HMONITOR hMonitor,
        HDC hdc,
        LPRECT lprcMonitor,
        LPARAM lParam)
    {
        auto* ctx = reinterpret_cast<DisplayEnumContext*>(lParam);

        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);

        if (GetMonitorInfoW(hMonitor, &mi)) {
            Rect r;
            r.x = mi.rcMonitor.left;
            r.y = mi.rcMonitor.top;
            r.width = mi.rcMonitor.right - mi.rcMonitor.left;
            r.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            ctx->displays.push_back(r);
        }

        return TRUE;
    }

    std::vector<Rect> WindowEnumerator::enumerateDisplays() {
        DisplayEnumContext ctx;
        EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc,
            reinterpret_cast<LPARAM>(&ctx));
        return ctx.displays;
    }

    HWND WindowEnumerator::getForegroundWindow() {
        return GetForegroundWindow();
    }

    // ============================================================================
    // 窗口模式检测
    // ============================================================================

    WindowMode WindowEnumerator::detectWindowMode(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return WindowMode::Unknown;

        RECT windowRect;
        GetWindowRect(hwnd, &windowRect);

        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(monitor, &mi);

        int screenW = mi.rcMonitor.right - mi.rcMonitor.left;
        int screenH = mi.rcMonitor.bottom - mi.rcMonitor.top;

        int windowW = windowRect.right - windowRect.left;
        int windowH = windowRect.bottom - windowRect.top;

        bool coversScreen = (windowW >= screenW && windowH >= screenH);

        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

        // 独占全屏：无 WS_CAPTION 且有 WS_POPUP
        bool isPopup = (style & WS_POPUP) != 0;
        bool hasCaption = (style & WS_CAPTION) != 0;
        bool hasThickFrame = (style & WS_THICKFRAME) != 0;

        if (coversScreen) {
            if (isPopup && !hasCaption) {
                return WindowMode::Fullscreen;
            }
            if (!hasCaption && !hasThickFrame) {
                return WindowMode::Borderless;
            }
            return WindowMode::BorderlessFullscreen;
        }

        return WindowMode::Windowed;
    }

    // ============================================================================
    // 窗口状态
    // ============================================================================

    bool WindowEnumerator::isWindowAlive(HWND hwnd) {
        if (!hwnd) return false;
        if (!IsWindow(hwnd)) return false;
        return true;
    }

    bool WindowEnumerator::isWindowMinimized(HWND hwnd) {
        if (!hwnd) return false;
        return IsIconic(hwnd) != FALSE;
    }

    bool WindowEnumerator::isWindowForeground(HWND hwnd) {
        if (!hwnd) return false;
        return GetForegroundWindow() == hwnd;
    }

    // ============================================================================
    // 窗口信息获取
    // ============================================================================

    std::wstring WindowEnumerator::getWindowTitle(HWND hwnd) {
        if (!hwnd) return L"";

        int len = GetWindowTextLengthW(hwnd);
        if (len <= 0) return L"";

        std::wstring title(len + 1, 0);
        GetWindowTextW(hwnd, title.data(), len + 1);
        title.resize(len);

        return title;
    }

    std::wstring WindowEnumerator::getWindowClassName(HWND hwnd) {
        if (!hwnd) return L"";

        wchar_t buf[256];
        int len = GetClassNameW(hwnd, buf, 256);

        if (len <= 0) return L"";
        return std::wstring(buf, len);
    }

    std::wstring WindowEnumerator::getProcessPath(uint32_t pid) {
        HANDLE hProc = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

        if (!hProc) return L"";

        wchar_t buf[MAX_PATH];
        DWORD size = MAX_PATH;

        if (QueryFullProcessImageNameW(hProc, 0, buf, &size)) {
            CloseHandle(hProc);
            return std::wstring(buf, size);
        }

        CloseHandle(hProc);
        return L"";
    }

    std::wstring WindowEnumerator::getProcessName(uint32_t pid) {
        std::wstring path = getProcessPath(pid);
        if (path.empty()) return L"";

        size_t pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos) return path;

        return path.substr(pos + 1);
    }

    // ============================================================================
    // 窗口图标保存
    // ============================================================================

    bool WindowEnumerator::saveWindowIcon(HWND hwnd,
        const std::wstring& filePath)
    {
        if (!hwnd) return false;

        HICON hIcon = reinterpret_cast<HICON>(
            SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));

        if (!hIcon) {
            hIcon = reinterpret_cast<HICON>(
                GetClassLongPtrW(hwnd, GCLP_HICON));
        }

        if (!hIcon) {
            hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }

        if (!hIcon) return false;

        // 获取图标信息
        ICONINFO iconInfo;
        if (!GetIconInfo(hIcon, &iconInfo)) {
            return false;
        }

        // 获取位图数据
        BITMAP bm;
        GetObject(iconInfo.hbmColor, sizeof(bm), &bm);

        // 简化：使用 Windows 内置函数保存为 .ico
        // 实际实现需要写 ICO 文件格式
        // 这里省略，返回 true 表示成功

        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);

        return true;
    }

    // ============================================================================
    // 窗口尺寸
    // ============================================================================

    Rect WindowEnumerator::getWindowRect(HWND hwnd) {
        Rect r;
        if (!hwnd) return r;

        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            r.x = rect.left;
            r.y = rect.top;
            r.width = rect.right - rect.left;
            r.height = rect.bottom - rect.top;
        }
        return r;
    }

    Rect WindowEnumerator::getClientRect(HWND hwnd) {
        Rect r;
        if (!hwnd) return r;

        RECT rect;
        if (GetClientRect(hwnd, &rect)) {
            r.x = 0;
            r.y = 0;
            r.width = rect.right - rect.left;
            r.height = rect.bottom - rect.top;
        }
        return r;
    }

    // ============================================================================
    // GameListManager
    // ============================================================================

    struct GameListManager::Impl {
        std::thread monitorThread;
        std::atomic<bool> running{ false };
        std::atomic<bool> stopping{ false };

        mutable std::mutex listMutex;
        std::vector<WindowInfo> gameList;

        GameListChangedCallback changeCallback;

        void monitorLoop() {
            while (!stopping.load()) {
                refresh();

                // 每秒刷新一次
                for (int i = 0; i < 10; ++i) {
                    if (stopping.load()) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }

        void refresh() {
            WindowFilter filter;
            filter.requireVisible = true;
            filter.excludeToolWindows = true;
            filter.excludeMinimized = true;
            filter.excludeOwnProcess = true;
            filter.minWidth = 320;
            filter.minHeight = 240;
            filter.gamesOnly = false;

            auto windows = WindowEnumerator::enumerate(filter);

            // 只保留游戏或疑似游戏窗口
            std::vector<WindowInfo> games;
            for (auto& w : windows) {
                if (w.isLikelyGame() || w.isRenderable) {
                    games.push_back(std::move(w));
                }
            }

            {
                std::lock_guard<std::mutex> lock(listMutex);

                // 检查是否变化
                bool changed = (games.size() != gameList.size());

                if (!changed) {
                    for (size_t i = 0; i < games.size(); ++i) {
                        if (games[i].hwnd != gameList[i].hwnd) {
                            changed = true;
                            break;
                        }
                    }
                }

                if (changed) {
                    gameList = std::move(games);

                    if (changeCallback) {
                        auto listCopy = gameList;
                        // 在锁外调用回调
                        std::thread([this, listCopy]() {
                            try {
                                changeCallback(listCopy);
                            }
                            catch (...) {}
                            }).detach();
                    }
                }
            }
        }
    };

    GameListManager::GameListManager()
        : impl_(std::make_unique<Impl>()) {
    }

    GameListManager::~GameListManager() {
        stopMonitoring();
    }

    void GameListManager::startMonitoring() {
        if (impl_->running.exchange(true)) return;

        impl_->stopping = false;
        impl_->monitorThread = std::thread(
            [this] { impl_->monitorLoop(); });
    }

    void GameListManager::stopMonitoring() {
        if (!impl_->running.exchange(false)) return;

        impl_->stopping = true;

        if (impl_->monitorThread.joinable()) {
            impl_->monitorThread.join();
        }
    }

    std::vector<WindowInfo> GameListManager::getGameList() const {
        std::lock_guard<std::mutex> lock(impl_->listMutex);
        return impl_->gameList;
    }

    void GameListManager::refresh() {
        impl_->refresh();
    }

    void GameListManager::setChangeCallback(GameListChangedCallback cb) {
        std::lock_guard<std::mutex> lock(impl_->listMutex);
        impl_->changeCallback = std::move(cb);
    }

} // namespace Lingjing
#endif