#include "flow/FlowEngineFactory.h"
#include "core/Logger.h"

#ifdef LJ_NVIDIA
#include "gpu/CudaInterop.h"
#endif

#ifdef LJ_INTEL
#include "gpu/SyclInterop.h"
#endif

#include <algorithm>

namespace Lingjing {

    // ============================================================================
    // 引擎可用性检查
    // ============================================================================

    bool FlowEngineFactory::isEngineAvailable(
        FlowEngineType type,
        const GpuInfo& gpu)
    {
        switch (type) {
        case FlowEngineType::HuaiZhu: {
            // 淮竹需要 AI 加速（用于深度估计）或者任何 GPU
            // 实际上任何现代 GPU 都可以运行，但如果支持 AI 加速更好
            return gpu.vendor == GpuVendor::NVIDIA ||
                gpu.vendor == GpuVendor::Intel;
        }

        case FlowEngineType::NVIDIA_NVOFA: {
#ifdef LJ_NVIDIA
            return gpu.vendor == GpuVendor::NVIDIA &&
                gpu.hasHardwareOpticalFlow();
#else
            return false;
#endif
        }

        case FlowEngineType::Intel_XeFlow: {
#ifdef LJ_INTEL
            return gpu.vendor == GpuVendor::Intel &&
                gpu.hasHardwareOpticalFlow();
#else
            return false;
#endif
        }

        case FlowEngineType::Variational: {
            // 变分光流可以在任何 GPU 上运行
            return gpu.vendor == GpuVendor::NVIDIA ||
                gpu.vendor == GpuVendor::Intel;
        }

        default:
            return false;
        }
    }

    // ============================================================================
    // 枚举引擎
    // ============================================================================

    std::vector<FlowEngineInfo> FlowEngineFactory::enumerateAvailable(
        const GpuInfo& gpu)
    {
        std::vector<FlowEngineInfo> infos;

        // 淮竹
        {
            FlowEngineInfo info;
            info.type = FlowEngineType::HuaiZhu;
            info.name = "淮竹 HuaiZhu";
            info.description = "几何反演 + 3D 相干流场 + 学习系统";
            info.requiredVendor = GpuVendor::Unknown;
            info.requiresHardwareOpticalFlow = false;
            info.requiresAIAcceleration = true;
            info.available = isEngineAvailable(FlowEngineType::HuaiZhu, gpu);
            info.estimatedQuality = 0.98f;
            info.estimatedLatencyMs = 0.62f;
            infos.push_back(info);
        }

        // NVIDIA NVOFA
        {
            FlowEngineInfo info;
            info.type = FlowEngineType::NVIDIA_NVOFA;
            info.name = "NVIDIA NVOFA";
            info.description = "NVIDIA 硬件光流加速器";
            info.requiredVendor = GpuVendor::NVIDIA;
            info.requiresHardwareOpticalFlow = true;
            info.requiresAIAcceleration = false;
            info.available = isEngineAvailable(FlowEngineType::NVIDIA_NVOFA, gpu);
            info.estimatedQuality = 0.92f;
            info.estimatedLatencyMs = 0.35f;
            infos.push_back(info);
        }

        // Intel Xe Flow
        {
            FlowEngineInfo info;
            info.type = FlowEngineType::Intel_XeFlow;
            info.name = "Intel Xe Flow";
            info.description = "Intel Xe 硬件光流单元";
            info.requiredVendor = GpuVendor::Intel;
            info.requiresHardwareOpticalFlow = true;
            info.requiresAIAcceleration = false;
            info.available = isEngineAvailable(FlowEngineType::Intel_XeFlow, gpu);
            info.estimatedQuality = 0.90f;
            info.estimatedLatencyMs = 0.40f;
            infos.push_back(info);
        }

        // 变分光流
        {
            FlowEngineInfo info;
            info.type = FlowEngineType::Variational;
            info.name = "Variational Flow";
            info.description = "双向对称变分光流";
            info.requiredVendor = GpuVendor::Unknown;
            info.requiresHardwareOpticalFlow = false;
            info.requiresAIAcceleration = false;
            info.available = isEngineAvailable(FlowEngineType::Variational, gpu);
            info.estimatedQuality = 0.85f;
            info.estimatedLatencyMs = 1.80f;
            infos.push_back(info);
        }

        return infos;
    }

    // ============================================================================
    // 创建引擎
    // ============================================================================

    std::unique_ptr<IFlowEngine> FlowEngineFactory::create(FlowEngineType type) {
        switch (type) {
        case FlowEngineType::HuaiZhu:
            return createHuaiZhuEngine();

        case FlowEngineType::NVIDIA_NVOFA:
            return createNVOFAEngine();

        case FlowEngineType::Intel_XeFlow:
            return createXeFlowEngine();

        case FlowEngineType::Variational:
            return createVariationalEngine();

        default:
            return nullptr;
        }
    }

    // ============================================================================
    // 选择最佳类型
    // ============================================================================

    FlowEngineType FlowEngineFactory::selectBestType(
        const GpuInfo& gpu,
        FlowEngineType preferredType)
    {
        // 用户指定优先
        if (preferredType != FlowEngineType::None) {
            if (isEngineAvailable(preferredType, gpu)) {
                return preferredType;
            }
            LOG_WARN("Preferred engine %s not available, falling back",
                flowEngineTypeName(preferredType));
        }

        // 按优先级自动选择：
        // 1. 淮竹（最高画质，通用）
        // 2. NVIDIA NVOFA（NVIDIA 硬件）
        // 3. Intel Xe Flow（Intel 硬件）
        // 4. 变分光流（回退）

        if (isEngineAvailable(FlowEngineType::HuaiZhu, gpu)) {
            return FlowEngineType::HuaiZhu;
        }

        if (isEngineAvailable(FlowEngineType::NVIDIA_NVOFA, gpu)) {
            return FlowEngineType::NVIDIA_NVOFA;
        }

        if (isEngineAvailable(FlowEngineType::Intel_XeFlow, gpu)) {
            return FlowEngineType::Intel_XeFlow;
        }

        return FlowEngineType::Variational;
    }

    std::unique_ptr<IFlowEngine> FlowEngineFactory::createBest(
        const GpuInfo& gpu,
        FlowEngineType preferredType)
    {
        FlowEngineType type = selectBestType(gpu, preferredType);

        LOG_INFO("Selected flow engine: %s", flowEngineTypeName(type));

        return create(type);
    }

} // namespace Lingjing

// ============================================================================
// 未实现引擎的存根（硬件后端不可用时返回 nullptr）
// ============================================================================

namespace Lingjing {
    std::unique_ptr<IFlowEngine> createNVOFAEngine() { return nullptr; }
    std::unique_ptr<IFlowEngine> createXeFlowEngine() { return nullptr; }
    std::unique_ptr<IFlowEngine> createVariationalEngine() { return nullptr; }
} // namespace Lingjing