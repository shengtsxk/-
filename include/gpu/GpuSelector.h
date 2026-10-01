#pragma once

#include "core/DeviceCaps.h"
#include <vector>

namespace Lingjing {

    // ============================================================================
    // GPU 选择策略
    // ============================================================================

    enum class GpuSelectionStrategy : uint32_t {
        HighestPerformance = 0,
        LowestPowerConsumption,
        PreferDiscrete,
        PreferIntegrated,
        PreferNvidia,
        PreferIntel,
        Manual,
    };

    // ============================================================================
    // 选择结果
    // ============================================================================

    struct GpuSelectionResult {
        int selectedIndex = -1;
        GpuInfo selectedGpu;
        std::string reason;
        float score = 0.0f;
        bool fallbackUsed = false;
    };

    // ============================================================================
    // GPU 选择器
    // ============================================================================

    class GpuSelector {
    public:
        // 从多个 GPU 中选择
        static GpuSelectionResult select(
            const std::vector<GpuInfo>& gpus,
            GpuSelectionStrategy strategy);

        // 手动选择（按索引）
        static GpuSelectionResult selectByIndex(
            const std::vector<GpuInfo>& gpus,
            int index);

        // 默认策略（NVIDIA 优先，然后 Intel，最后按性能）
        static GpuSelectionResult selectDefault(
            const std::vector<GpuInfo>& gpus);

        // 检查指定 GPU 是否满足帧生成要求
        static bool meetsRequirements(const GpuInfo& gpu);

        // 检查是否满足最低配置
        static bool isMinimumConfig(const GpuInfo& gpu);

        // 获取失败原因
        static std::string getRequirementFailureReason(const GpuInfo& gpu);
    };

} // namespace Lingjing