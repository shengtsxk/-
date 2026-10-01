#include "gpu/GpuSelector.h"
#include "core/Logger.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace Lingjing {

    // ============================================================================
    // 评分函数
    // ============================================================================

    static float computePerformanceScore(const GpuInfo& gpu) {
        float score = 0.0f;

        // 显存（最多 40 分）
        float vramGB = static_cast<float>(gpu.dedicatedVramBytes) /
            (1024.0f * 1024.0f * 1024.0f);
        score += std::min(vramGB * 8.0f, 40.0f);

        // 计算单元（最多 20 分）
        score += std::min(static_cast<float>(gpu.computeUnits) * 0.01f, 20.0f);

        // AI 算力（最多 30 分）
        score += std::min(gpu.aiTopsInt8 * 0.05f, 30.0f);

        // 硬件光流（+20 分）
        if (gpu.hasHardwareOpticalFlow()) score += 20.0f;

        // 独显（+10 分）
        if (gpu.isDiscrete) score += 10.0f;

        return score;
    }

    static float computePowerScore(const GpuInfo& gpu) {
        // 简单：核显分数高
        float score = 0.0f;

        if (!gpu.isDiscrete) {
            score += 50.0f;
        }

        // 计算单元少 = 功耗低
        float cuNorm = static_cast<float>(gpu.computeUnits) / 100.0f;
        score += std::max(0.0f, 30.0f - cuNorm);

        return score;
    }

    // ============================================================================
    // 选择
    // ============================================================================

    GpuSelectionResult GpuSelector::select(
        const std::vector<GpuInfo>& gpus,
        GpuSelectionStrategy strategy)
    {
        GpuSelectionResult result;

        if (gpus.empty()) {
            result.reason = "No GPUs available";
            return result;
        }

        switch (strategy) {
        case GpuSelectionStrategy::HighestPerformance:
            return selectDefault(gpus);

        case GpuSelectionStrategy::LowestPowerConsumption: {
            float bestScore = -std::numeric_limits<float>::max();
            for (size_t i = 0; i < gpus.size(); ++i) {
                float s = computePowerScore(gpus[i]);
                if (s > bestScore) {
                    bestScore = s;
                    result.selectedIndex = static_cast<int>(i);
                    result.selectedGpu = gpus[i];
                }
            }
            result.score = bestScore;
            result.reason = "Selected for lowest power consumption";
            break;
        }

        case GpuSelectionStrategy::PreferDiscrete: {
            float bestScore = -std::numeric_limits<float>::max();
            for (size_t i = 0; i < gpus.size(); ++i) {
                if (!gpus[i].isDiscrete) continue;
                float s = computePerformanceScore(gpus[i]);
                if (s > bestScore) {
                    bestScore = s;
                    result.selectedIndex = static_cast<int>(i);
                    result.selectedGpu = gpus[i];
                }
            }

            if (result.selectedIndex < 0) {
                // 回退到默认
                result = selectDefault(gpus);
                result.fallbackUsed = true;
            }
            else {
                result.score = bestScore;
                result.reason = "Selected discrete GPU";
            }
            break;
        }

        case GpuSelectionStrategy::PreferIntegrated: {
            float bestScore = -std::numeric_limits<float>::max();
            for (size_t i = 0; i < gpus.size(); ++i) {
                if (gpus[i].isDiscrete) continue;
                float s = computePerformanceScore(gpus[i]);
                if (s > bestScore) {
                    bestScore = s;
                    result.selectedIndex = static_cast<int>(i);
                    result.selectedGpu = gpus[i];
                }
            }

            if (result.selectedIndex < 0) {
                result = selectDefault(gpus);
                result.fallbackUsed = true;
            }
            else {
                result.score = bestScore;
                result.reason = "Selected integrated GPU";
            }
            break;
        }

        case GpuSelectionStrategy::PreferNvidia: {
            float bestScore = -std::numeric_limits<float>::max();
            for (size_t i = 0; i < gpus.size(); ++i) {
                if (gpus[i].vendor != GpuVendor::NVIDIA) continue;
                float s = computePerformanceScore(gpus[i]);
                if (s > bestScore) {
                    bestScore = s;
                    result.selectedIndex = static_cast<int>(i);
                    result.selectedGpu = gpus[i];
                }
            }

            if (result.selectedIndex < 0) {
                result = selectDefault(gpus);
                result.fallbackUsed = true;
            }
            else {
                result.score = bestScore;
                result.reason = "Selected NVIDIA GPU";
            }
            break;
        }

        case GpuSelectionStrategy::PreferIntel: {
            float bestScore = -std::numeric_limits<float>::max();
            for (size_t i = 0; i < gpus.size(); ++i) {
                if (gpus[i].vendor != GpuVendor::Intel) continue;
                float s = computePerformanceScore(gpus[i]);
                if (s > bestScore) {
                    bestScore = s;
                    result.selectedIndex = static_cast<int>(i);
                    result.selectedGpu = gpus[i];
                }
            }

            if (result.selectedIndex < 0) {
                result = selectDefault(gpus);
                result.fallbackUsed = true;
            }
            else {
                result.score = bestScore;
                result.reason = "Selected Intel GPU";
            }
            break;
        }

        case GpuSelectionStrategy::Manual:
        default:
            return selectByIndex(gpus, 0);
        }

        if (result.selectedIndex >= 0) {
            LOG_INFO("GPU selection: %s (score=%.1f, reason=%s)",
                result.selectedGpu.name.c_str(),
                result.score,
                result.reason.c_str());
        }

        return result;
    }

    // ============================================================================
    // 默认策略
    // ============================================================================

    GpuSelectionResult GpuSelector::selectDefault(
        const std::vector<GpuInfo>& gpus)
    {
        GpuSelectionResult result;

        if (gpus.empty()) {
            result.reason = "No GPUs available";
            return result;
        }

        // 优先级：NVIDIA + 硬件光流 > Intel Arc + 硬件光流
        //       > 其他独显 > 核显

        float bestScore = -std::numeric_limits<float>::max();

        for (size_t i = 0; i < gpus.size(); ++i) {
            const auto& gpu = gpus[i];

            float score = computePerformanceScore(gpu);

            // 厂商加成
            if (gpu.vendor == GpuVendor::NVIDIA) score += 100.0f;
            if (gpu.vendor == GpuVendor::Intel) score += 50.0f;

            // 帧生成必要条件
            if (!gpu.hasHardwareOpticalFlow() && !gpu.hasAIAcceleration()) {
                score -= 200.0f;  // 降级
            }

            if (score > bestScore) {
                bestScore = score;
                result.selectedIndex = static_cast<int>(i);
                result.selectedGpu = gpu;
            }
        }

        result.score = bestScore;
        result.reason = "Default selection (performance + vendor priority)";

        if (result.selectedIndex >= 0) {
            LOG_INFO("GPU selected: %s (score=%.1f)",
                result.selectedGpu.name.c_str(), result.score);
        }

        return result;
    }

    // ============================================================================
    // 按索引选择
    // ============================================================================

    GpuSelectionResult GpuSelector::selectByIndex(
        const std::vector<GpuInfo>& gpus,
        int index)
    {
        GpuSelectionResult result;

        if (index < 0 || index >= static_cast<int>(gpus.size())) {
            result.reason = "Invalid GPU index";
            return result;
        }

        result.selectedIndex = index;
        result.selectedGpu = gpus[index];
        result.score = computePerformanceScore(gpus[index]);
        result.reason = "Manual selection";

        return result;
    }

    // ============================================================================
    // 需求检查
    // ============================================================================

    bool GpuSelector::meetsRequirements(const GpuInfo& gpu) {
        // 最低要求：
        // - 至少 2 GB 显存
        // - 支持硬件光流或 AI 加速

        if (gpu.dedicatedVramBytes < 2ULL * 1024 * 1024 * 1024) {
            return false;
        }

        if (!gpu.hasHardwareOpticalFlow() && !gpu.hasAIAcceleration()) {
            return false;
        }

        return true;
    }

    bool GpuSelector::isMinimumConfig(const GpuInfo& gpu) {
        // 最低配置：
        // - NVIDIA GTX 1660+
        // - Intel Arc A380+
        // - 4 GB 显存

        if (gpu.dedicatedVramBytes < 4ULL * 1024 * 1024 * 1024) {
            return false;
        }

        if (!gpu.hasHardwareOpticalFlow()) {
            return false;
        }

        return true;
    }

    std::string GpuSelector::getRequirementFailureReason(const GpuInfo& gpu) {
        std::ostringstream oss;

        if (gpu.dedicatedVramBytes < 2ULL * 1024 * 1024 * 1024) {
            oss << "Insufficient VRAM (< 2 GB). ";
        }

        if (!gpu.hasHardwareOpticalFlow() && !gpu.hasAIAcceleration()) {
            oss << "No hardware optical flow or AI acceleration. ";
        }

        if (oss.str().empty()) {
            return "OK";
        }

        return oss.str();
    }

} // namespace Lingjing

// ============================================================================
// GPU 上下文存根（CUDA/SYCL 未编译时返回 nullptr）
// ============================================================================

#include "gpu/IGpuContext.h"

namespace Lingjing {
#ifndef LJ_NVIDIA
    std::unique_ptr<IGpuContext> createCudaContext() { return nullptr; }
#endif
#ifndef LJ_INTEL
    std::unique_ptr<IGpuContext> createSyclContext() { return nullptr; }
#endif
} // namespace Lingjing
