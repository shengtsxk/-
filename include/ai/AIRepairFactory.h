#pragma once

#include "ai/IAIRepair.h"
#include "core/DeviceCaps.h"

#include <memory>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 可用的 AI 修复后端信息
    // ============================================================================

    struct AIRepairBackendInfo {
        AIRepairBackend backend = AIRepairBackend::None;
        std::string name;
        std::string description;
        GpuVendor requiredVendor = GpuVendor::Unknown;
        bool available = false;
        float estimatedLatencyMs = 0.0f;
        float estimatedQuality = 0.0f;
    };

    // ============================================================================
    // AI 修复工厂
    // ============================================================================

    class AIRepairFactory {
    public:
        // 枚举所有可用后端
        static std::vector<AIRepairBackendInfo> enumerateAvailable(
            const GpuInfo& gpu);

        // 选择最佳后端
        static AIRepairBackend selectBestBackend(
            const GpuInfo& gpu,
            AIRepairBackend preferred = AIRepairBackend::None);

        // 创建 AI 修复
        static std::unique_ptr<IAIRepair> create(
            AIRepairBackend backend);

        // 自动选择并创建
        static std::unique_ptr<IAIRepair> createBest(
            const GpuInfo& gpu,
            const AIRepairConfig& config,
            AIRepairBackend preferred = AIRepairBackend::None);

        // 检查后端可用性
        static bool isBackendAvailable(
            AIRepairBackend backend,
            const GpuInfo& gpu);

        // 使用 ModelRegistry 自动选择模型
        static AIRepairConfig autoConfigure(const GpuInfo& gpu,
            const std::string& preferredModel = "");
    };

} // namespace Lingjing