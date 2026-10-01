#include "gpu/InteropManager.h"
#include "core/Logger.h"

#ifdef LJ_NVIDIA
#include "gpu/CudaInterop.h"
#endif

#ifdef LJ_INTEL
#include "gpu/SyclInterop.h"
#endif

#include <mutex>
#include <unordered_map>
#include <atomic>

namespace Lingjing {

    // ============================================================================
    // 实现
    // ============================================================================

    struct InteropManager::Impl {
        IGpuContext* gpuContext = nullptr;
        void* d3d11Device = nullptr;
        void* d3d11Context = nullptr;

        std::mutex mutex;

#ifdef LJ_NVIDIA
        std::unique_ptr<CudaInterop> cudaInterop;
#endif

#ifdef LJ_INTEL
        std::unique_ptr<SyclInterop> syclInterop;
#endif

        struct TextureEntry {
            void* d3d11Texture;
            void* interopHandle;
            uint32_t width;
            uint32_t height;
            TextureFormat format;
            bool mapped;
        };

        std::unordered_map<void*, TextureEntry> textures;  // keyed by interopHandle
        std::unordered_map<void*, void*> textureToHandle;  // d3d11Texture -> interopHandle

        std::atomic<size_t> activeMappings{ 0 };
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    InteropManager::InteropManager()
        : impl_(std::make_unique<Impl>()) {
    }

    InteropManager::~InteropManager() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool InteropManager::initialize(IGpuContext* gpuContext,
        void* d3d11Device,
        void* d3d11Context)
    {
        if (initialized_) return true;

        if (!gpuContext || !d3d11Device) {
            LOG_ERROR("InteropManager: null arguments");
            return false;
        }

        std::lock_guard<std::mutex> lock(impl_->mutex);

        impl_->gpuContext = gpuContext;
        impl_->d3d11Device = d3d11Device;
        impl_->d3d11Context = d3d11Context;

        switch (gpuContext->vendor()) {
#ifdef LJ_NVIDIA
        case GpuVendor::NVIDIA: {
            impl_->cudaInterop = std::make_unique<CudaInterop>();
            if (!impl_->cudaInterop->initialize(d3d11Device)) {
                LOG_ERROR("Failed to initialize CUDA-D3D11 interop");
                return false;
            }
            break;
        }
#endif

#ifdef LJ_INTEL
        case GpuVendor::Intel: {
            impl_->syclInterop = std::make_unique<SyclInterop>();
            if (!impl_->syclInterop->initialize(
                d3d11Device, d3d11Context,
                nullptr /* sycl queue 由调用方传入 */)) {
                LOG_ERROR("Failed to initialize SYCL-D3D11 interop");
                return false;
            }
            break;
        }
#endif

        default:
            LOG_ERROR("Unsupported GPU vendor for interop");
            return false;
        }

        initialized_ = true;
        LOG_INFO("InteropManager initialized for %s",
            gpuContext->backendName());

        return true;
    }

    void InteropManager::shutdown() {
        if (!initialized_) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

#ifdef LJ_NVIDIA
        if (impl_->cudaInterop) {
            impl_->cudaInterop->shutdown();
            impl_->cudaInterop.reset();
        }
#endif

#ifdef LJ_INTEL
        if (impl_->syclInterop) {
            impl_->syclInterop->shutdown();
            impl_->syclInterop.reset();
        }
#endif

        impl_->textures.clear();
        impl_->textureToHandle.clear();
        impl_->activeMappings = 0;

        initialized_ = false;
    }

    // ============================================================================
    // 纹理注册
    // ============================================================================

    void* InteropManager::registerTexture(void* d3d11Texture,
        uint32_t width,
        uint32_t height,
        TextureFormat format)
    {
        if (!initialized_ || !d3d11Texture) return nullptr;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        // 检查是否已注册
        auto it = impl_->textureToHandle.find(d3d11Texture);
        if (it != impl_->textureToHandle.end()) {
            return it->second;
        }

        void* handle = nullptr;

#ifdef LJ_NVIDIA
        if (impl_->cudaInterop) {
            void* cudaResource = nullptr;
            if (!impl_->cudaInterop->registerTexture(d3d11Texture, &cudaResource)) {
                return nullptr;
            }
            handle = cudaResource;
        }
#endif

#ifdef LJ_INTEL
        if (impl_->syclInterop) {
            // 需要先获取共享句柄
            // 简化：直接使用纹理指针
            handle = d3d11Texture;
        }
#endif

        if (!handle) return nullptr;

        Impl::TextureEntry entry;
        entry.d3d11Texture = d3d11Texture;
        entry.interopHandle = handle;
        entry.width = width;
        entry.height = height;
        entry.format = format;
        entry.mapped = false;

        impl_->textures[handle] = entry;
        impl_->textureToHandle[d3d11Texture] = handle;

        LOG_DEBUG("Registered texture: %p (handle=%p, %ux%u)",
            d3d11Texture, handle, width, height);

        return handle;
    }

    void InteropManager::unregisterTexture(void* interopHandle) {
        if (!initialized_ || !interopHandle) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        auto it = impl_->textures.find(interopHandle);
        if (it == impl_->textures.end()) return;

        void* d3d11Texture = it->second.d3d11Texture;

#ifdef LJ_NVIDIA
        if (impl_->cudaInterop) {
            impl_->cudaInterop->unregisterTexture(interopHandle);
        }
#endif

#ifdef LJ_INTEL
        if (impl_->syclInterop) {
            impl_->syclInterop->releaseImage(interopHandle);
        }
#endif

        impl_->textures.erase(it);
        impl_->textureToHandle.erase(d3d11Texture);
    }

    // ============================================================================
    // 映射
    // ============================================================================

    void* InteropManager::mapTexture(void* interopHandle, IGpuStream& stream) {
        if (!initialized_ || !interopHandle) return nullptr;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        auto it = impl_->textures.find(interopHandle);
        if (it == impl_->textures.end()) return nullptr;

#ifdef LJ_NVIDIA
        if (impl_->cudaInterop) {
            void* resources[] = { interopHandle };
            if (!impl_->cudaInterop->mapResources(resources, 1,
                stream.nativeHandle())) {
                return nullptr;
            }

            void* devicePtr = nullptr;
            size_t size = 0;

            if (!impl_->cudaInterop->getMappedPointer(interopHandle,
                &devicePtr,
                &size)) {
                return nullptr;
            }

            it->second.mapped = true;
            impl_->activeMappings++;

            return devicePtr;
        }
#endif

#ifdef LJ_INTEL
        if (impl_->syclInterop) {
            it->second.mapped = true;
            impl_->activeMappings++;
            return interopHandle;
        }
#endif

        return nullptr;
    }

    bool InteropManager::unmapTexture(void* interopHandle, IGpuStream& stream) {
        if (!initialized_ || !interopHandle) return false;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        auto it = impl_->textures.find(interopHandle);
        if (it == impl_->textures.end()) return false;

#ifdef LJ_NVIDIA
        if (impl_->cudaInterop) {
            void* resources[] = { interopHandle };
            if (!impl_->cudaInterop->unmapResources(resources, 1,
                stream.nativeHandle())) {
                return false;
            }
        }
#endif

        it->second.mapped = false;
        if (impl_->activeMappings > 0) {
            impl_->activeMappings--;
        }

        return true;
    }

    // ============================================================================
    // 查询
    // ============================================================================

    GpuVendor InteropManager::vendor() const {
        if (!impl_->gpuContext) return GpuVendor::Unknown;
        return impl_->gpuContext->vendor();
    }

    size_t InteropManager::registeredTextureCount() const {
        return impl_->textures.size();
    }

    size_t InteropManager::activeMappingCount() const {
        return impl_->activeMappings.load();
    }

} // namespace Lingjing