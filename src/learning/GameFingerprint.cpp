#include "learning/GameFingerprint.h"
#include "core/Logger.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
#endif

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // FNV-1a 哈希
        // ============================================================================

        uint64_t GameFingerprint::fnv1a(const void* data, size_t size,
            uint64_t seed)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            uint64_t hash = seed;

            for (size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ULL;
            }

            return hash;
        }

        uint64_t GameFingerprint::hashString(const std::string& str) {
            return fnv1a(str.data(), str.size());
        }

        uint64_t GameFingerprint::hashWString(const std::wstring& str) {
            return fnv1a(str.data(), str.size() * sizeof(wchar_t));
        }

        // ============================================================================
        // 采集指纹
        // ============================================================================

#ifdef _WIN32

        GameFingerprint GameFingerprint::capture(void* hwnd) {
            GameFingerprint fp;

            if (!hwnd || !IsWindow(static_cast<HWND>(hwnd))) {
                return fp;
            }

            HWND h = static_cast<HWND>(hwnd);

            // 1. 进程信息
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);

            HANDLE hProc = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

            if (hProc) {
                wchar_t buf[MAX_PATH];
                DWORD size = MAX_PATH;

                if (QueryFullProcessImageNameW(hProc, 0, buf, &size)) {
                    fp.processPath = std::wstring(buf, size);
                    fp.processHash = hashWString(fp.processPath);
                }

                CloseHandle(hProc);
            }

            // 2. 窗口类名
            wchar_t className[256];
            int clsLen = GetClassNameW(h, className, 256);

            if (clsLen > 0) {
                fp.windowClassName = std::wstring(className, clsLen);
                fp.windowClassHash = hashWString(fp.windowClassName);
            }

            // 3. 窗口标题
            wchar_t title[512];
            int titleLen = GetWindowTextW(h, title, 512);

            if (titleLen > 0) {
                fp.windowTitle = std::wstring(title, titleLen);
            }

            // 4. 窗口尺寸
            RECT rect;
            GetClientRect(h, &rect);
            fp.width = static_cast<uint32_t>(rect.right - rect.left);
            fp.height = static_cast<uint32_t>(rect.bottom - rect.top);

            // 5. 刷新率
            HMONITOR monitor = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW mi = {};
            mi.cbSize = sizeof(mi);

            if (GetMonitorInfoW(monitor, &mi)) {
                DEVMODEW dm = {};
                dm.dmSize = sizeof(dm);

                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                    fp.refreshRate = dm.dmDisplayFrequency;
                }
            }

            // 6. GPU 信息
            ComPtr<IDXGIFactory6> factory;
            HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

            if (SUCCEEDED(hr)) {
                ComPtr<IDXGIAdapter1> adapter;

                if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    fp.gpuHash = fnv1a(&desc.VendorId, sizeof(desc.VendorId));
                    fp.gpuHash ^= fnv1a(&desc.DeviceId, sizeof(desc.DeviceId),
                        fp.gpuHash);
                }
            }

            // 7. 计算综合哈希
            fp.computeCombinedHash();

            fp.valid = (fp.processHash != 0);

            return fp;
        }

#endif

        GameFingerprint GameFingerprint::captureFromProcess(
            const std::wstring& path,
            uint32_t width,
            uint32_t height)
        {
            GameFingerprint fp;

            fp.processPath = path;
            fp.processHash = hashWString(path);
            fp.width = width;
            fp.height = height;

            // GPU 信息
#ifdef _WIN32
            ComPtr<IDXGIFactory6> factory;
            HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

            if (SUCCEEDED(hr)) {
                ComPtr<IDXGIAdapter1> adapter;

                if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    fp.gpuHash = fnv1a(&desc.VendorId, sizeof(desc.VendorId));
                    fp.gpuHash ^= fnv1a(&desc.DeviceId, sizeof(desc.DeviceId),
                        fp.gpuHash);
                }
            }
#endif

            fp.computeCombinedHash();
            fp.valid = (fp.processHash != 0);

            return fp;
        }

        // ============================================================================
        // 组合哈希
        // ============================================================================

        void GameFingerprint::computeCombinedHash() {
            combinedHash = fnv1a(&processHash, sizeof(processHash));
            combinedHash = fnv1a(&windowClassHash, sizeof(windowClassHash),
                combinedHash);
            combinedHash = fnv1a(&gpuHash, sizeof(gpuHash), combinedHash);
        }

        // ============================================================================
        // 序列化
        // ============================================================================

        std::string GameFingerprint::toString() const {
            std::ostringstream oss;

            oss << "GameFingerprint{";
            oss << "hash=0x" << std::hex << std::setw(16) << std::setfill('0')
                << combinedHash;
            oss << ", process=0x" << std::setw(16) << processHash;
            oss << ", size=" << std::dec << width << "x" << height;
            oss << ", hz=" << refreshRate;
            oss << ", valid=" << (valid ? "true" : "false");
            oss << "}";

            return oss.str();
        }

        // ============================================================================
        // 比较
        // ============================================================================

        bool GameFingerprint::matches(const GameFingerprint& other) const {
            return combinedHash == other.combinedHash;
        }

        float GameFingerprint::similarity(const GameFingerprint& other) const {
            float score = 0.0f;
            int count = 0;

            // 进程匹配（权重 50%）
            if (processHash == other.processHash) {
                score += 0.5f;
            }
            count++;

            // GPU 匹配（权重 20%）
            if (gpuHash == other.gpuHash) {
                score += 0.2f;
            }
            count++;

            // 窗口类名匹配（权重 10%）
            if (windowClassHash == other.windowClassHash) {
                score += 0.1f;
            }
            count++;

            // 尺寸匹配（权重 10%）
            if (width == other.width && height == other.height) {
                score += 0.1f;
            }
            count++;

            // 刷新率匹配（权重 10%）
            if (refreshRate == other.refreshRate) {
                score += 0.1f;
            }
            count++;

            return score;
        }

    } // namespace Learning
} // namespace Lingjing