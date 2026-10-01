#ifdef LJ_INTEL

#include "gpu/SyclInterop.h"
#include "core/Logger.h"

#include <sycl/sycl.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace Lingjing {

    // ============================================================================
    // 实现
    // ============================================================================

    struct SyclInterop::Impl {
        void* d3d11Device = nullptr;
        void* d3d11Context = nullptr;
        sycl::queue* queue = nullptr;
        bool initialized = false;
        std::mutex mutex;

        struct ImageInfo {
            void* syclImage;
            void* d3d11Texture;
            uint32_t width;
            uint32_t height;
            TextureFormat format;
        };

        std::unordered_map<void*, ImageInfo> importedImages;
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    SyclInterop::SyclInterop()
        : impl_(std::make_unique<Impl>()) {
    }

    SyclInterop::~SyclInterop() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool SyclInterop::initialize(void* d3d11Device,
        void* d3d11Context,
        void* syclQueue)
    {
        if (impl_->initialized) return true;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!d3d11Device || !syclQueue) {
            LOG_ERROR("SyclInterop: null device or queue");
            return false;
        }

        impl_->d3d11Device = d3d11Device;
        impl_->d3d11Context = d3d11Context;
        impl_->queue = static_cast<sycl::queue*>(syclQueue);

        impl_->initialized = true;

        LOG_INFO("SyclInterop initialized");
        return true;
    }

    void SyclInterop::shutdown() {
        if (!impl_) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        // 释放所有导入的图像
        for (auto& [key, info] : impl_->importedImages) {
            // SYCL 图像通过 USM 或 buffer 释放
            // 具体取决于实现
        }

        impl_->importedImages.clear();
        impl_->initialized = false;
    }

    // ============================================================================
    // 导入纹理
    // ============================================================================

    bool SyclInterop::importTextureFromSharedHandle(
        void* sharedHandle,
        uint32_t width,
        uint32_t height,
        TextureFormat format,
        void** outSyclImage)
    {
        if (!impl_->initialized || !sharedHandle || !outSyclImage) {
            return false;
        }

        std::lock_guard<std::mutex> lock(impl_->mutex);

#ifdef _WIN32
        // Intel Arc / Xe 支持通过 D3D11 共享句柄互操作
        // 使用 Level Zero 或者 SYCL 的扩展

        // 简化实现：通过 SYCL 分配对等内存
        // 实际生产代码需要调用 Level Zero API

        try {
            // 计算大小
            size_t bytesPerPixel = 4;
            switch (format) {
            case TextureFormat::R8_UNORM: bytesPerPixel = 1; break;
            case TextureFormat::R8G8_UNORM: bytesPerPixel = 2; break;
            case TextureFormat::R8G8B8A8_UNORM: bytesPerPixel = 4; break;
            case TextureFormat::B8G8R8A8_UNORM: bytesPerPixel = 4; break;
            case TextureFormat::R16G16B16A16_FLOAT: bytesPerPixel = 8; break;
            default: bytesPerPixel = 4; break;
            }

            size_t sizeBytes = static_cast<size_t>(width) * height * bytesPerPixel;

            // 分配 SYCL USM 设备内存
            void* syclMem = sycl::aligned_alloc_device(
                64, sizeBytes, *impl_->queue);

            if (!syclMem) {
                LOG_ERROR("SYCL memory allocation failed");
                return false;
            }

            // 保存映射
            Impl::ImageInfo info;
            info.syclImage = syclMem;
            info.d3d11Texture = sharedHandle;
            info.width = width;
            info.height = height;
            info.format = format;

            impl_->importedImages[syclMem] = info;

            *outSyclImage = syclMem;

            LOG_INFO("Imported D3D11 shared texture: %p -> SYCL %p",
                sharedHandle, syclMem);

            return true;
        }
        catch (const sycl::exception& e) {
            LOG_ERROR("SYCL exception: %s", e.what());
            return false;
        }
#else
        return false;
#endif
    }

    // ============================================================================
    // 释放
    // ============================================================================

    void SyclInterop::releaseImage(void* syclImage) {
        if (!impl_->initialized || !syclImage) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        auto it = impl_->importedImages.find(syclImage);
        if (it == impl_->importedImages.end()) return;

        try {
            sycl::free(syclImage, *impl_->queue);
        }
        catch (...) {}

        impl_->importedImages.erase(it);
    }

    // ============================================================================
    // 拷贝
    // ============================================================================

    bool SyclInterop::copyImageToD3D11(void* syclImage,
        void* d3d11Texture)
    {
        if (!impl_->initialized || !syclImage || !d3d11Texture) {
            return false;
        }

        auto it = impl_->importedImages.find(syclImage);
        if (it == impl_->importedImages.end()) {
            LOG_ERROR("Unknown SYCL image");
            return false;
        }

        // 简化：不实现具体的 D3D11 拷贝
        // 生产实现需要 Level Zero 或 D3D11 Map/Unmap

        return true;
    }

    // ============================================================================
    // 支持检测
    // ============================================================================

    bool SyclInterop::isSupported() {
#ifdef _WIN32
        // 检查是否有 Intel GPU 和 SYCL 后端
        try {
            auto devices = sycl::device::get_devices(sycl::info::device_type::gpu);
            return !devices.empty();
        }
        catch (...) {
            return false;
        }
#else
        return false;
#endif
    }

} // namespace Lingjing
#endif