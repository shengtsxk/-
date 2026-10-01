#include "core/DeviceCaps.h"
#include "core/Logger.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#ifdef LJ_NVIDIA
#include <nvml.h>
#endif

namespace Lingjing {

    // ============================================================================
    // GpuInfo 显示
    // ============================================================================

    std::string GpuInfo::toDisplayString() const {
        std::ostringstream oss;
        oss << name;

        if (dedicatedVramBytes > 0) {
            oss << " (" << (dedicatedVramBytes / (1024 * 1024)) << " MB)";
        }

        if (hasHardwareOpticalFlow()) oss << " [HW-Flow]";
        if (hasTensorCores()) oss << " [Tensor]";
        if (hasXmx()) oss << " [XMX]";

        return oss.str();
    }

    std::string GpuInfo::toDetailedString() const {
        std::ostringstream oss;

        oss << "=== " << name << " ===\n";
        oss << "  Vendor: " << gpuVendorName(vendor) << "\n";
        oss << "  Vendor ID: 0x" << std::hex << vendorId << std::dec << "\n";
        oss << "  Device ID: 0x" << std::hex << deviceId << std::dec << "\n";
        oss << "  Driver: " << driverVersion << "\n";

        oss << "  Dedicated VRAM: "
            << (dedicatedVramBytes / (1024 * 1024)) << " MB\n";
        oss << "  Shared Memory: "
            << (sharedMemoryBytes / (1024 * 1024)) << " MB\n";

        oss << "  Compute Units: " << computeUnits << "\n";
        oss << "  Max Clock: " << maxClockMhz << " MHz\n";

        if (aiTopsInt8 > 0.0f) {
            oss << "  AI TOPS (INT8): " << aiTopsInt8 << "\n";
            oss << "  AI TOPS (FP16): " << aiTopsFp16 << "\n";
        }

        if (hasHardwareOpticalFlow()) {
            oss << "  Hardware Flow: " << maxFlowWidth << "x" << maxFlowHeight << "\n";
        }

        oss << "  Features:";
        if (hasHardwareOpticalFlow()) oss << " HW-Flow";
        if (hasFeature(features, GpuFeature::ForwardBackwardFlow)) oss << " FB-Flow";
        if (hasTensorCores()) oss << " Tensor";
        if (hasXmx()) oss << " XMX";
        if (hasFeature(features, GpuFeature::D3D11Interop)) oss << " D3D11";
        if (hasFeature(features, GpuFeature::D3D12Interop)) oss << " D3D12";
        oss << "\n";

        return oss.str();
    }

    float GpuInfo::score() const {
        float s = 0.0f;

        // 显存评分（最多 40 分）
        float vramGB = static_cast<float>(dedicatedVramBytes) /
            (1024.0f * 1024.0f * 1024.0f);
        s += std::min(vramGB * 8.0f, 40.0f);

        // 计算单元评分（最多 20 分）
        s += std::min(static_cast<float>(computeUnits) * 0.005f, 20.0f);

        // AI 算力评分（最多 30 分）
        s += std::min(aiTopsInt8 * 0.05f, 30.0f);

        // 硬件光流加分
        if (hasHardwareOpticalFlow()) s += 20.0f;

        // 独显加分
        if (isDiscrete) s += 10.0f;

        return s;
    }

    // ============================================================================
    // DXGI 枚举
    // ============================================================================

#ifdef _WIN32

    static std::vector<GpuInfo> enumerateViaDxgi() {
        std::vector<GpuInfo> gpus;

        ComPtr<IDXGIFactory6> factory6;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory6));

        if (FAILED(hr)) {
            LOG_ERROR("CreateDXGIFactory1 failed: 0x%08X", hr);

            return gpus;
        }

        UINT index = 0;
        ComPtr<IDXGIAdapter1> adapter;

        while (factory6->EnumAdapterByGpuPreference(
            index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC1 desc;
            hr = adapter->GetDesc1(&desc);

            if (FAILED(hr)) {
                ++index;
                continue;
            }

            // 跳过软件适配器
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                ++index;
                continue;
            }

            GpuInfo info;
            info.adapterIndex = index;
            info.vendorId = desc.VendorId;
            info.deviceId = desc.DeviceId;
            info.subsystemId = desc.SubSysId;
            info.revision = desc.Revision;
            info.dedicatedVramBytes = desc.DedicatedVideoMemory;
            info.sharedMemoryBytes = desc.SharedSystemMemory;

            // 名称转换
            char nameUtf8[256];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                nameUtf8, sizeof(nameUtf8),
                nullptr, nullptr);
            info.name = nameUtf8;

            // 厂商识别
            switch (desc.VendorId) {
            case 0x10DE: info.vendor = GpuVendor::NVIDIA; break;
            case 0x8086: info.vendor = GpuVendor::Intel;  break;
            case 0x1002:
            case 0x1022: info.vendor = GpuVendor::AMD;    break;
            case 0x1414: info.vendor = GpuVendor::Microsoft; break;
            case 0x5143: info.vendor = GpuVendor::Qualcomm; break;
            default:     info.vendor = GpuVendor::Unknown;  break;
            }

            // LUID 字符串
            std::wostringstream luid;
            luid << std::hex << desc.AdapterLuid.HighPart << ":"
                << std::hex << desc.AdapterLuid.LowPart;
            info.adapterLuidStr = luid.str();

            // 独显判定（显存 > 512MB 且不是核显）
            info.isDiscrete = (desc.DedicatedVideoMemory > 512ULL * 1024 * 1024);

            gpus.push_back(std::move(info));
            ++index;
        }

        return gpus;
    }

#endif

    // ============================================================================
    // NVIDIA 探测
    // ============================================================================

    bool probeNvidiaGpu(GpuInfo& info) {
#ifdef LJ_NVIDIA
        nvmlReturn_t ret = nvmlInit_v2();
        if (ret != NVML_SUCCESS) {
            LOG_WARN("NVML init failed: %s", nvmlErrorString(ret));
            return false;
        }

        uint32_t deviceCount = 0;
        nvmlDeviceGetCount_v2(&deviceCount);

        bool found = false;

        for (uint32_t i = 0; i < deviceCount; ++i) {
            nvmlDevice_t device;
            ret = nvmlDeviceGetHandleByIndex_v2(i, &device);
            if (ret != NVML_SUCCESS) continue;

            nvmlPciInfo_t pci;
            ret = nvmlDeviceGetPciInfo_v3(device, &pci);
            if (ret != NVML_SUCCESS) continue;

            if (pci.deviceId != info.deviceId) continue;

            // 驱动版本
            char driverVersion[80];
            if (nvmlSystemGetDriverVersion(driverVersion,
                sizeof(driverVersion)) == NVML_SUCCESS) {
                info.driverVersion = driverVersion;
            }

            // 核心数
            uint32_t cores = 0;
            if (nvmlDeviceGetNumGpuCores(device, &cores) == NVML_SUCCESS) {
                info.computeUnits = cores;
            }

            // 时钟
            uint32_t clock = 0;
            if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS,
                &clock) == NVML_SUCCESS) {
                info.maxClockMhz = clock;
            }

            if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM,
                &clock) == NVML_SUCCESS) {
                info.memoryClockMhz = clock;
            }

            // 显存
            nvmlMemory_t mem;
            if (nvmlDeviceGetMemoryInfo(device, &mem) == NVML_SUCCESS) {
                info.dedicatedVramBytes = mem.total;
            }

            // 判断架构（通过 deviceId）
            // Turing: 0x1E00~0x1FFF
            // Ampere: 0x2200~0x25FF
            // Ada: 0x2600~0x2EFF
            // Blackwell: 0x2F00+
            uint32_t did = info.deviceId;

            bool isTuring = (did >= 0x1E00 && did < 0x2000);
            bool isAmpere = (did >= 0x2200 && did < 0x2600);
            bool isAda = (did >= 0x2600 && did < 0x2F00);
            bool isBlackwell = (did >= 0x2F00);
            if (isTuring || isAmpere || isAda || isBlackwell) {
                info.features = info.features |
                    GpuFeature::HardwareOpticalFlow |
                    GpuFeature::ForwardBackwardFlow |
                    GpuFeature::GlobalFlowVector |
                    GpuFeature::TensorCores |
                    GpuFeature::D3D11Interop |
                    GpuFeature::D3D12Interop |
                    GpuFeature::VulkanInterop |
                    GpuFeature::NvencH264 |
                    GpuFeature::NvencH265 |
                    GpuFeature::DlssSupport;

                if (isBlackwell) {
                    info.features = info.features | GpuFeature::NvencAV1;
                    info.aiTopsInt8 = 1330.0f;
                    info.aiTopsFp16 = 665.0f;
                    info.aiTopsFp32 = 166.0f;
                }
                else if (isAda) {
                    info.features = info.features | GpuFeature::NvencAV1;
                    info.aiTopsInt8 = 660.0f;
                    info.aiTopsFp16 = 330.0f;
                    info.aiTopsFp32 = 82.5f;
                }
                else if (isAmpere) {
                    info.aiTopsInt8 = 260.0f;
                    info.aiTopsFp16 = 130.0f;
                    info.aiTopsFp32 = 32.5f;
                }
                else {
                    info.aiTopsInt8 = 130.0f;
                    info.aiTopsFp16 = 65.0f;
                    info.aiTopsFp32 = 16.3f;
                }

                info.maxFlowWidth = 3840;
                info.maxFlowHeight = 2160;
                info.maxFlowGridSize = 4;
            }

            found = true;
            break;
        }

        nvmlShutdown();
        return found;
#else
        (void)info;
        return false;
#endif
    }

    // ============================================================================
    // Intel 探测
    // ============================================================================

    bool probeIntelGpu(GpuInfo& info) {
        uint32_t did = info.deviceId;

        // Intel Arc A 系列（DG2）: 0x56xx
        bool isArcA = (did >= 0x5600 && did < 0x5700);

        // Intel Arc B 系列（BMG）: 0xE2xx
        bool isArcB = (did >= 0xE200 && did < 0xE300);

        // Intel Xe-LPG 核显: 0x7Dxx
        bool isXeLPG = (did >= 0x7D00 && did < 0x7E00);

        // Intel Xe-LP 核显: 0x9Axx
        bool isXeLP = (did >= 0x9A00 && did < 0x9B00);

        if (!(isArcA || isArcB || isXeLPG || isXeLP)) {
            return false;
        }

        // 基础特性
        info.features = info.features |
            GpuFeature::D3D11Interop |
            GpuFeature::D3D12Interop |
            GpuFeature::VulkanInterop |
            GpuFeature::LevelZeroInterop |
            GpuFeature::XeSSSupport;

        // Xe-LPG / Arc 系列有 XMX
        if (isArcA || isArcB || isXeLPG) {
            info.features = info.features | GpuFeature::XmxEngines;
        }

        // 独显：Arc A / B
        if (isArcA || isArcB) {
            info.isDiscrete = true;
            info.features = info.features |
                GpuFeature::HardwareOpticalFlow |
                GpuFeature::ForwardBackwardFlow;

            info.maxFlowWidth = 3840;
            info.maxFlowHeight = 2160;
            info.maxFlowGridSize = 4;
        }

        // AI 算力
        if (isArcB) {
            info.aiTopsInt8 = 560.0f;
            info.aiTopsFp16 = 280.0f;
            info.aiTopsFp32 = 70.0f;
        }
        else if (isArcA) {
            info.aiTopsInt8 = 280.0f;
            info.aiTopsFp16 = 140.0f;
            info.aiTopsFp32 = 35.0f;
        }
        else {
            info.aiTopsInt8 = 70.0f;
            info.aiTopsFp16 = 35.0f;
            info.aiTopsFp32 = 8.8f;
        }

        return true;
    }

    // ============================================================================
    // 综合探测
    // ============================================================================

    std::vector<GpuInfo> detectAllGpus() {
#ifdef _WIN32
        auto gpus = enumerateViaDxgi();
#else
        std::vector<GpuInfo> gpus;
#endif

        for (auto& gpu : gpus) {
            switch (gpu.vendor) {
            case GpuVendor::NVIDIA:
                probeNvidiaGpu(gpu);
                break;
            case GpuVendor::Intel:
                probeIntelGpu(gpu);
                break;
            default:
                break;
            }

            LOG_INFO("GPU detected: %s", gpu.toDisplayString().c_str());
        }


        // 模拟 GPU 测试模式：LJ_SIM_GPU=RTX4060 时注入虚拟 RTX 4060
        // （仅用于流程验证；虚拟显卡无 CUDA 计算能力，无法替代真实硬件）
        const char* simGpu = std::getenv("LJ_SIM_GPU");
        if (simGpu && std::string(simGpu) == "RTX4060") {
            LOG_WARN("SIMULATION MODE: 注入虚拟 RTX 4060 进行流程测试");
            gpus.clear();
            GpuInfo g;
            g.vendor = GpuVendor::NVIDIA;
            g.name = "NVIDIA GeForce RTX 4060 (Simulated)";
            g.dedicatedVramBytes = 8ULL * 1024 * 1024 * 1024;
            g.computeUnits = 3072;
            g.aiTopsInt8 = 660.0f;
            g.aiTopsFp16 = 330.0f;
            g.aiTopsFp32 = 82.5f;
            g.features = GpuFeature::HardwareOpticalFlow |
                GpuFeature::ForwardBackwardFlow |
                GpuFeature::GlobalFlowVector |
                GpuFeature::TensorCores |
                GpuFeature::D3D11Interop |
                GpuFeature::D3D12Interop |
                GpuFeature::VulkanInterop |
                GpuFeature::NvencH264 |
                GpuFeature::NvencH265 |
                GpuFeature::NvencAV1 |
                GpuFeature::DlssSupport;
            g.maxFlowWidth = 3840;
            g.maxFlowHeight = 2160;
            g.maxFlowGridSize = 4;
            g.isDiscrete = true;
            g.isPrimary = true;
            gpus.push_back(g);
        }

        return gpus;
    }

    GpuInfo selectBestGpu(const std::vector<GpuInfo>& gpus,
        bool preferNvidia,
        bool preferIntel)
    {
        if (gpus.empty()) return GpuInfo{};

        const GpuInfo* best = nullptr;
        float bestScore = -1.0f;

        for (const auto& gpu : gpus) {
            float s = gpu.score();

            if (preferNvidia && gpu.vendor == GpuVendor::NVIDIA) {
                s += 100.0f;
            }
            if (preferIntel && gpu.vendor == GpuVendor::Intel) {
                s += 80.0f;
            }

            if (s > bestScore) {
                bestScore = s;
                best = &gpu;
            }
        }

        if (best) {
            LOG_INFO("Selected GPU: %s (score=%.1f)",
                best->name.c_str(), bestScore);
            return *best;
        }

        return gpus[0];
    }

    GpuInfo queryGpuDetail(const GpuInfo& basic) {
        GpuInfo detailed = basic;

        switch (detailed.vendor) {
        case GpuVendor::NVIDIA:
            probeNvidiaGpu(detailed);
            break;
        case GpuVendor::Intel:
            probeIntelGpu(detailed);
            break;
        default:
            break;
        }

        return detailed;
    }

} // namespace Lingjing
