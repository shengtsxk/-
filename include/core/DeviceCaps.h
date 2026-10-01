#pragma once

#include "Types.h"
#include <string>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // GPU 厂商
    // ============================================================================

    enum class GpuVendor : uint32_t {
        Unknown = 0,
        NVIDIA,
        Intel,
        AMD,
        Microsoft,
        Qualcomm,
    };

    inline const char* gpuVendorName(GpuVendor v) {
        switch (v) {
        case GpuVendor::NVIDIA:    return "NVIDIA";
        case GpuVendor::Intel:     return "Intel";
        case GpuVendor::AMD:       return "AMD";
        case GpuVendor::Microsoft: return "Microsoft";
        case GpuVendor::Qualcomm:  return "Qualcomm";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // GPU 加速特性（位掩码）
    // ============================================================================

    enum class GpuFeature : uint64_t {
        None = 0,

        // 光流能力
        HardwareOpticalFlow = 1ULL << 0,
        ForwardBackwardFlow = 1ULL << 1,
        GlobalFlowVector = 1ULL << 2,

        // AI 加速
        TensorCores = 1ULL << 10,
        XmxEngines = 1ULL << 11,
        DlaAccelerator = 1ULL << 12,

        // 视频编码
        NvencH264 = 1ULL << 20,
        NvencH265 = 1ULL << 21,
        NvencAV1 = 1ULL << 22,

        // 互操作
        D3D11Interop = 1ULL << 30,
        D3D12Interop = 1ULL << 31,
        VulkanInterop = 1ULL << 32,
        LevelZeroInterop = 1ULL << 33,

        // 超分技术
        DlssSupport = 1ULL << 40,
        XeSSSupport = 1ULL << 41,
        FsrSupport = 1ULL << 42,
    };

    inline GpuFeature operator|(GpuFeature a, GpuFeature b) {
        return static_cast<GpuFeature>(
            static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
    }

    inline GpuFeature operator&(GpuFeature a, GpuFeature b) {
        return static_cast<GpuFeature>(
            static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
    }

    inline bool hasFeature(GpuFeature flags, GpuFeature f) {
        return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(f)) != 0;
    }

    // ============================================================================
    // GPU 信息
    // ============================================================================

    struct GpuInfo {
        // 标识
        GpuVendor vendor = GpuVendor::Unknown;
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        uint32_t subsystemId = 0;
        uint32_t revision = 0;
        std::string name;
        std::string driverVersion;
        std::string driverDate;

        // 显存
        uint64_t dedicatedVramBytes = 0;
        uint64_t sharedMemoryBytes = 0;
        uint64_t totalVramBytes = 0;

        // 计算能力
        uint32_t computeUnits = 0;      // CUDA 核心 / Xe 核心
        uint32_t maxClockMhz = 0;
        uint32_t memoryClockMhz = 0;
        uint32_t memoryBusWidth = 0;

        // AI 算力（TOPS）
        float aiTopsInt8 = 0.0f;
        float aiTopsFp16 = 0.0f;
        float aiTopsFp32 = 0.0f;

        // 光流能力
        uint32_t maxFlowWidth = 0;
        uint32_t maxFlowHeight = 0;
        uint32_t maxFlowGridSize = 0;

        // 特性集合
        GpuFeature features = GpuFeature::None;

        // 标识
        bool isDiscrete = false;
        bool isPrimary = false;
        uint32_t adapterIndex = 0;
        std::wstring adapterLuidStr;

        // ====================================================================
        // 查询
        // ====================================================================

        bool hasHardwareOpticalFlow() const {
            return hasFeature(features, GpuFeature::HardwareOpticalFlow);
        }

        bool hasTensorCores() const {
            return hasFeature(features, GpuFeature::TensorCores);
        }

        bool hasXmx() const {
            return hasFeature(features, GpuFeature::XmxEngines);
        }

        bool hasAIAcceleration() const {
            return hasTensorCores() || hasXmx();
        }

        bool supportsInterop() const {
            return hasFeature(features, GpuFeature::D3D11Interop) ||
                hasFeature(features, GpuFeature::D3D12Interop);
        }

        std::string toDisplayString() const;
        std::string toDetailedString() const;

        // 计算一个综合评分（用于选择最佳 GPU）
        float score() const;
    };

    // ============================================================================
    // 探测接口
    // ============================================================================

    std::vector<GpuInfo> detectAllGpus();

    GpuInfo selectBestGpu(const std::vector<GpuInfo>& gpus,
        bool preferNvidia = true,
        bool preferIntel = true);

    GpuInfo queryGpuDetail(const GpuInfo& basic);

    // NVIDIA 专用
    bool probeNvidiaGpu(GpuInfo& info);

    // Intel 专用
    bool probeIntelGpu(GpuInfo& info);

} // namespace Lingjing