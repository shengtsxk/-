#pragma once
#ifdef _WIN32

#include "core/Types.h"
#include <vector>
#include <functional>
#include <string>

#include <windows.h>

namespace Lingjing {

    // ============================================================================
    // 窗口信息
    // ============================================================================

    struct WindowInfo {
        HWND hwnd = nullptr;
        std::wstring title;
        std::wstring className;
        std::wstring processName;
        std::wstring processPath;
        uint32_t processId = 0;

        Rect windowRect;
        Rect clientRect;
        Size frameSize;

        WindowMode mode = WindowMode::Unknown;
        bool visible = false;
        bool minimized = false;
        bool foreground = false;
        bool topmost = false;
        bool hasIcon = false;

        // GPU 渲染判定
        bool isRenderable = false;   // 是否是游戏或视频窗口

        std::string toString() const;
        bool isLikelyGame() const;
    };

    // ============================================================================
    // 窗口过滤器
    // ============================================================================

    struct WindowFilter {
        bool requireVisible = true;
        bool excludeToolWindows = true;
        bool excludeMinimized = false;
        bool excludeOwnProcess = true;
        uint32_t minWidth = 320;
        uint32_t minHeight = 240;
        bool requireTitle = true;
        bool gamesOnly = false;
    };

    // ============================================================================
    // 枚举器
    // ============================================================================

    class WindowEnumerator {
    public:
        // 枚举所有窗口
        static std::vector<WindowInfo> enumerate(
            const WindowFilter& filter = WindowFilter{});

        // 查找特定窗口
        static WindowInfo findWindow(HWND hwnd);

        // 枚举所有显示器
        static std::vector<Rect> enumerateDisplays();

        // 获取前台窗口
        static HWND getForegroundWindow();

        // 窗口模式判定
        static WindowMode detectWindowMode(HWND hwnd);

        // 窗口状态
        static bool isWindowAlive(HWND hwnd);
        static bool isWindowMinimized(HWND hwnd);
        static bool isWindowForeground(HWND hwnd);

        // 窗口标题
        static std::wstring getWindowTitle(HWND hwnd);
        static std::wstring getWindowClassName(HWND hwnd);
        static std::wstring getProcessPath(uint32_t pid);
        static std::wstring getProcessName(uint32_t pid);

        // 图标
        static bool saveWindowIcon(HWND hwnd, const std::wstring& filePath);

        // 窗口大小
        static Rect getWindowRect(HWND hwnd);
        static Rect getClientRect(HWND hwnd);
    };

    // ============================================================================
    // 游戏列表管理
    // ============================================================================

    class GameListManager {
    public:
        GameListManager();
        ~GameListManager();

        // 开始监控新窗口
        void startMonitoring();

        // 停止监控
        void stopMonitoring();

        // 获取当前游戏列表
        std::vector<WindowInfo> getGameList() const;

        // 手动刷新
        void refresh();

        // 回调
        using GameListChangedCallback =
            std::function<void(const std::vector<WindowInfo>&)>;

        void setChangeCallback(GameListChangedCallback cb);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing
#endif