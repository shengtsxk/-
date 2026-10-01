#pragma once

#include "flow/IFlowEngine.h"
#include "core/DeviceCaps.h"

#include <memory>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 引擎信息
    // ============================================================================

    struct FlowEngineInfo {
        FlowEngineType type = FlowEngineType::None;
        std::string name;
        std::string description;
        GpuVendor requiredVendor = GpuVendor::Unknown;
        bool requiresHardwareOpticalFlow = false;
        bool requiresAIAcceleration = false;
        bool available = false;
        float estimatedQuality = 0.0f;
        float estimatedLatencyMs = 0.0f;
    };

    // ============================================================================
    // 引擎工厂
    // ============================================================================

    class FlowEngineFactory {
    public:
        // 枚举所有可用的光流引擎
        static std::vector<FlowEngineInfo> enumerateAvailable(
            const GpuInfo& gpu);

        // 创建指定类型的引擎
        static std::unique_ptr<IFlowEngine> create(
            FlowEngineType type);

        // 自动选择并创建最佳引擎
        static std::unique_ptr<IFlowEngine> createBest(
            const GpuInfo& gpu,
            FlowEngineType preferredType = FlowEngineType::None);

        // 选择最佳引擎类型（不创建）
        static FlowEngineType selectBestType(
            const GpuInfo& gpu,
            FlowEngineType preferredType = FlowEngineType::None);

        // 检查引擎是否可用
        static bool isEngineAvailable(
            FlowEngineType type,
            const GpuInfo& gpu);
    };

} // namespace Lingjing