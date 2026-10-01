#ifdef _WIN32

#include "capture/CaptureSelector.h"
#include "capture/WGCCapture.h"
#include "capture/DXGICapture.h"
#include "capture/HookedCapture.h"
#include "core/Logger.h"

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cwctype>

namespace Lingjing {

    // ============================================================================
    // 反作弊检测
    // ============================================================================

    bool CaptureSelector::detectAntiCheat(uint32_t processId) {
        HANDLE hProc = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE, processId);

        if (!hProc) return false;

        bool detected = false;

        HMODULE modules[1024];
        DWORD needed;

        if (EnumProcessModules(hProc, modules, sizeof(modules), &needed)) {
            DWORD count = needed / sizeof(HMODULE);

            static const wchar_t* antiCheatNames[] = {
                L"EasyAntiCheat",
                L"BEClient",
                L"BEDaisy",
                L"vgc.dll",
                L"vgk.sys",
                L"Ricochet",
                L"PnkBstr",
                L"FairFight",
                L"GameGuard",
                L"XignCode",
                L"ACE-",
                L"TenProtect",
                L"TenSafe",
                L"nProtect",
            };

            for (DWORD i = 0; i < count && !detected; ++i) {
                wchar_t name[MAX_PATH];
                if (GetModuleBaseNameW(hProc, modules[i], name, MAX_PATH)) {
                    for (const wchar_t* ac : antiCheatNames) {
                        if (wcsstr(name, ac) != nullptr) {
                            detected = true;
                            break;
                        }
                    }
                }
            }
        }

        CloseHandle(hProc);
        return detected;
    }

    // ============================================================================
    // 建议
    // ============================================================================

    CaptureSuggestion CaptureSelector::suggest(const WindowInfo& target) {
        CaptureSuggestion suggestion;

        if (!target.hwnd) {
            suggestion.backend = CaptureBackend::Auto;
            suggestion.reason = "Invalid target";
            return suggestion;
        }

        bool hasAntiCheat = detectAntiCheat(target.processId);

        // 根据窗口模式决定
        switch (target.mode) {
        case WindowMode::Fullscreen:
            if (!hasAntiCheat) {
                suggestion.backend = CaptureBackend::Hook;
                suggestion.antiCheatSafe = false;
                suggestion.estimatedLatencyMs = 0.5f;
                suggestion.estimatedQuality = 1.0f;
                suggestion.reason = "Fullscreen + no anti-cheat detected: "
                    "Hook backend for lowest latency";
            }
            else {
                suggestion.backend = CaptureBackend::DXGI_DD;
                suggestion.antiCheatSafe = true;
                suggestion.estimatedLatencyMs = 1.0f;
                suggestion.estimatedQuality = 0.98f;
                suggestion.reason = "Fullscreen + anti-cheat: "
                    "DXGI Desktop Duplication";
            }
            break;

        case WindowMode::BorderlessFullscreen:
        case WindowMode::Borderless:
        case WindowMode::Windowed:
        default:
            suggestion.backend = CaptureBackend::WGC;
            suggestion.antiCheatSafe = true;
            suggestion.estimatedLatencyMs = 1.2f;
            suggestion.estimatedQuality = 0.99f;
            suggestion.reason = "Windowed/Borderless: "
                "Windows Graphics Capture";
            break;
        }

        // 反作弊强制降级
        if (hasAntiCheat && suggestion.backend == CaptureBackend::Hook) {
            suggestion.backend = CaptureBackend::WGC;
            suggestion.reason += " (anti-cheat detected, downgraded)";
        }

        return suggestion;
    }

    // ============================================================================
    // 创建
    // ============================================================================

    std::unique_ptr<ICapture> CaptureSelector::createCapture(
        const WindowInfo& target,
        CaptureBackend preferredBackend)
    {
        CaptureBackend backend = preferredBackend;

        if (backend == CaptureBackend::Auto) {
            backend = autoSelectBackend(target);
        }

        LOG_INFO("Creating capture: backend=%s, target='%s'",
            captureBackendName(backend),
            target.toString().c_str());

        switch (backend) {
        case CaptureBackend::WGC:
            return createWGCCapture();

        case CaptureBackend::DXGI_DD:
            return createDXGICapture();

        case CaptureBackend::Hook:
            return createHookedCapture();

        default:
            return createWGCCapture();
        }
    }

    // ============================================================================
    // Hook 安全检查
    // ============================================================================

    bool CaptureSelector::isHookSafe(const WindowInfo& target) {
        if (detectAntiCheat(target.processId)) {
            return false;
        }
        return true;
    }

    // ============================================================================
    // 自动选择
    // ============================================================================

    CaptureBackend CaptureSelector::autoSelectBackend(const WindowInfo& target) {
        auto suggestion = suggest(target);
        return suggestion.backend;
    }

} // namespace Lingjing
#endif