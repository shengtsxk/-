#include "ai/AIRepairFactory.h"
#include "ai/ModelLoader.h"
#include "core/Logger.h"

namespace Lingjing {

    // ============================================================================
    // 后端可用性
    // ============================================================================

    bool AIRepairFactory::isBackendAvailable(
        AIRepairBackend backend,
        const GpuInfo& gpu)
    {
        switch (backend) {
        case AIRepairBackend::TensorRT:
#ifdef LJ_NVIDIA
            return gpu.vendor == GpuVendor::NVIDIA &&
                (gpu.hasTensorCores() || gpu.hasHardwareOpticalFlow());
#else
            return false;
#endif

        case AIRepairBackend::OpenVINO:
#ifdef LJ_INTEL
            return gpu.vendor == GpuVendor::Intel &&
                (gpu.hasXmx() || gpu.hasHardwareOpticalFlow());
#else
            return false;
#endif

        case AIRepairBackend::DirectML:
            // DirectML 通用支持（未实现）
            return false;

        default:
            return false;
        }
    }

    // ============================================================================
    // 枚举后端
    // ============================================================================

    std::vector<AIRepairBackendInfo> AIRepairFactory::enumerateAvailable(
        const GpuInfo& gpu)
    {
        std::vector<AIRepairBackendInfo> infos;

        // TensorRT
        {
            AIRepairBackendInfo info;
            info.backend = AIRepairBackend::TensorRT;
            info.name = "TensorRT";
            info.description = "NVIDIA TensorRT FP16/INT8 推理";
            info.requiredVendor = GpuVendor::NVIDIA;
            info.available = isBackendAvailable(AIRepairBackend::TensorRT, gpu);
            info.estimatedLatencyMs = 0.5f;
            info.estimatedQuality = 0.98f;
            infos.push_back(info);
        }

        // OpenVINO
        {
            AIRepairBackendInfo info;
            info.backend = AIRepairBackend::OpenVINO;
            info.name = "OpenVINO";
            info.description = "Intel OpenVINO XMX 加速推理";
            info.requiredVendor = GpuVendor::Intel;
            info.available = isBackendAvailable(AIRepairBackend::OpenVINO, gpu);
            info.estimatedLatencyMs = 0.6f;
            info.estimatedQuality = 0.97f;
            infos.push_back(info);
        }

        // DirectML
        {
            AIRepairBackendInfo info;
            info.backend = AIRepairBackend::DirectML;
            info.name = "DirectML";
            info.description = "Windows DirectML 通用推理";
            info.requiredVendor = GpuVendor::Unknown;
            info.available = isBackendAvailable(AIRepairBackend::DirectML, gpu);
            info.estimatedLatencyMs = 1.0f;
            info.estimatedQuality = 0.92f;
            infos.push_back(info);
        }

        return infos;
    }

    // ============================================================================
    // 选择最佳后端
    // ============================================================================

    AIRepairBackend AIRepairFactory::selectBestBackend(
        const GpuInfo& gpu,
        AIRepairBackend preferred)
    {
        if (preferred != AIRepairBackend::None) {
            if (isBackendAvailable(preferred, gpu)) {
                return preferred;
            }
            LOG_WARN("Preferred backend not available, falling back");
        }

        // NVIDIA 优先 TensorRT
        if (gpu.vendor == GpuVendor::NVIDIA &&
            isBackendAvailable(AIRepairBackend::TensorRT, gpu)) {
            return AIRepairBackend::TensorRT;
        }

        // Intel 优先 OpenVINO
        if (gpu.vendor == GpuVendor::Intel &&
            isBackendAvailable(AIRepairBackend::OpenVINO, gpu)) {
            return AIRepairBackend::OpenVINO;
        }

        return AIRepairBackend::None;
    }

    // ============================================================================
    // 创建
    // ============================================================================

    std::unique_ptr<IAIRepair> AIRepairFactory::create(
        AIRepairBackend backend)
    {
        switch (backend) {
        case AIRepairBackend::TensorRT:
#ifdef LJ_NVIDIA
            return createTensorRTAIRepair();
#else
            return nullptr;
#endif

        case AIRepairBackend::OpenVINO:
#ifdef LJ_INTEL
            return createOpenVINOAIREpair();
#else
            return nullptr;
#endif

        default:
            return nullptr;
        }
    }

    // ============================================================================
    // 自动配置
    // ============================================================================

    AIRepairConfig AIRepairFactory::autoConfigure(
        const GpuInfo& gpu,
        const std::string& preferredModel)
    {
        AIRepairConfig config;

        // 后端选择
        config.backend = selectBestBackend(gpu);

        // 精度
        if (gpu.vendor == GpuVendor::NVIDIA) {
            config.precision = ModelPrecision::FP16;
            config.enableFp16 = true;
        }
        else if (gpu.vendor == GpuVendor::Intel) {
            config.precision = ModelPrecision::FP16;
            config.enableFp16 = true;
        }

        // 自动选择模型
        ModelRegistry registry;

        std::string modelPath;

        if (!preferredModel.empty()) {
            const auto* entry = registry.findByName(preferredModel);
            if (entry && entry->available) {
                modelPath = entry->path;
            }
        }

        if (modelPath.empty()) {
            const auto* entry = registry.recommendedModel();
            if (entry) {
                modelPath = entry->path;
            }
        }

        config.modelPath = modelPath;

        // 缓存目录
        config.cacheDir = ModelLoader::getModelCacheDirectory();
        config.enableEngineCache = true;

        // 性能
        config.workspaceSizeBytes = 512ULL * 1024 * 1024;

        return config;
    }

    // ============================================================================
    // 创建最佳
    // ============================================================================

    std::unique_ptr<IAIRepair> AIRepairFactory::createBest(
        const GpuInfo& gpu,
        const AIRepairConfig& config,
        AIRepairBackend preferred)
    {
        AIRepairBackend backend = selectBestBackend(gpu, preferred);

        if (backend == AIRepairBackend::None) {
            LOG_WARN("No AI repair backend available for this GPU");
            return nullptr;
        }

        auto repair = create(backend);

        if (!repair) {
            LOG_ERROR("Failed to create AI repair backend");
            return nullptr;
        }

        if (!repair->initialize(nullptr, config)) {
            LOG_ERROR("Failed to initialize AI repair");
            return nullptr;
        }

        LOG_INFO("AI repair created: %s", repair->engineName());

        return repair;
    }

} // namespace Lingjing