#pragma once
#ifdef _WIN32

#include "capture/ICapture.h"
#include "capture/WindowEnumerator.h"
#include <memory>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 捕获方案建议
    // ============================================================================

    struct CaptureSuggestion {
        CaptureBackend backend = CaptureBackend::Auto;
        bool antiCheatSafe = true;
        float estimatedLatencyMs = 0.0f;
        float estimatedQuality = 1.0f;
        std::string reason;
    };

    // ============================================================================
    // 捕获选择器
    // ============================================================================

    class CaptureSelector {
    public:
        // 根据目标窗口选择最佳捕获后端
        static CaptureSuggestion suggest(const WindowInfo& target);

        // 创建捕获对象
        static std::unique_ptr<ICapture> createCapture(
            const WindowInfo& target,
            CaptureBackend preferredBackend = CaptureBackend::Auto);

        // 检查是否可安全使用 Hook 后端
        static bool isHookSafe(const WindowInfo& target);

        // 自动选择
        static CaptureBackend autoSelectBackend(const WindowInfo& target);

    private:
        static bool detectAntiCheat(uint32_t processId);
    };

} // namespace Lingjing
#endif