#ifdef LJ_NVIDIA

#include "gpu/CudaInterop.h"
#include "core/Logger.h"

#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>

#include <unordered_map>
#include <mutex>

namespace Lingjing {

    // ============================================================================
    // 实现
    // ============================================================================

    struct CudaInterop::Impl {
        void* d3d11Device = nullptr;
        bool initialized = false;
        std::mutex mutex;

        // 已注册的资源
        struct ResourceInfo {
            cudaGraphicsResource_t resource;
            void* d3d11Texture;
            bool mapped;
        };

        std::unordered_map<void*, ResourceInfo> registeredResources;
        std::atomic<int> activeMappings{ 0 };
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    CudaInterop::CudaInterop()
        : impl_(std::make_unique<Impl>()) {
    }

    CudaInterop::~CudaInterop() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool CudaInterop::initialize(void* d3d11Device) {
        if (impl_->initialized) return true;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!d3d11Device) {
            LOG_ERROR("CudaInterop: null D3D11 device");
            return false;
        }

        impl_->d3d11Device = d3d11Device;

        // 设置 CUDA 使用 D3D11 设备
        cudaError_t err = cudaD3D11SetDirect3DDevice(d3d11Device);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaD3D11SetDirect3DDevice failed: %s",
                cudaGetErrorString(err));
            return false;
        }

        impl_->initialized = true;
        LOG_INFO("CudaInterop initialized");
        return true;
    }

    void CudaInterop::shutdown() {
        if (!impl_) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        // 注销所有资源
        for (auto& [key, info] : impl_->registeredResources) {
            try {
                if (info.mapped) {
                    cudaGraphicsUnmapResources(1, &info.resource, 0);
                }
                cudaGraphicsUnregisterResource(info.resource);
            }
            catch (...) {}
        }

        impl_->registeredResources.clear();
        impl_->initialized = false;
    }

    // ============================================================================
    // 注册纹理
    // ============================================================================

    bool CudaInterop::registerTexture(void* d3d11Texture,
        void** outCudaResource)
    {
        if (!impl_->initialized) {
            LOG_ERROR("CudaInterop not initialized");
            return false;
        }

        if (!d3d11Texture || !outCudaResource) return false;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        // 检查是否已注册
        auto it = impl_->registeredResources.find(d3d11Texture);
        if (it != impl_->registeredResources.end()) {
            *outCudaResource = it->second.resource;
            return true;
        }

        // 注册
        cudaGraphicsResource_t resource = nullptr;
        cudaError_t err = cudaGraphicsD3D11RegisterResource(
            &resource,
            d3d11Texture,
            cudaGraphicsRegisterFlagsNone);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaGraphicsD3D11RegisterResource failed: %s",
                cudaGetErrorString(err));
            return false;
        }

        // 保存
        Impl::ResourceInfo info;
        info.resource = resource;
        info.d3d11Texture = d3d11Texture;
        info.mapped = false;

        impl_->registeredResources[d3d11Texture] = info;

        *outCudaResource = resource;

        LOG_DEBUG("Registered D3D11 texture: %p -> %p",
            d3d11Texture, resource);

        return true;
    }

    // ============================================================================
    // 注销
    // ============================================================================

    bool CudaInterop::unregisterTexture(void* cudaResource) {
        if (!impl_->initialized || !cudaResource) return false;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        // 查找
        for (auto it = impl_->registeredResources.begin();
            it != impl_->registeredResources.end(); ++it) {

            if (it->second.resource == cudaResource) {
                if (it->second.mapped) {
                    cudaGraphicsUnmapResources(1, &it->second.resource, 0);
                }

                cudaError_t err =
                    cudaGraphicsUnregisterResource(it->second.resource);

                if (err != cudaSuccess) {
                    LOG_WARN("cudaGraphicsUnregisterResource failed: %s",
                        cudaGetErrorString(err));
                }

                impl_->registeredResources.erase(it);
                return true;
            }
        }

        return false;
    }

    // ============================================================================
    // 映射
    // ============================================================================

    bool CudaInterop::mapResources(void** cudaResources,
        int count,
        void* cudaStream)
    {
        if (!impl_->initialized || !cudaResources || count <= 0) return false;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        std::vector<cudaGraphicsResource_t> resources(count);

        for (int i = 0; i < count; ++i) {
            resources[i] = static_cast<cudaGraphicsResource_t>(cudaResources[i]);
        }

        cudaStream_t stream = static_cast<cudaStream_t>(cudaStream);

        cudaError_t err = cudaGraphicsMapResources(
            count,
            resources.data(),
            stream);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaGraphicsMapResources failed: %s",
                cudaGetErrorString(err));
            return false;
        }

        // 标记为已映射
        for (int i = 0; i < count; ++i) {
            for (auto& [key, info] : impl_->registeredResources) {
                if (info.resource == resources[i]) {
                    info.mapped = true;
                    break;
                }
            }
        }

        impl_->activeMappings++;

        return true;
    }

    bool CudaInterop::unmapResources(void** cudaResources,
        int count,
        void* cudaStream)
    {
        if (!impl_->initialized || !cudaResources || count <= 0) return false;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        std::vector<cudaGraphicsResource_t> resources(count);

        for (int i = 0; i < count; ++i) {
            resources[i] = static_cast<cudaGraphicsResource_t>(cudaResources[i]);
        }

        cudaStream_t stream = static_cast<cudaStream_t>(cudaStream);

        cudaError_t err = cudaGraphicsUnmapResources(
            count,
            resources.data(),
            stream);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaGraphicsUnmapResources failed: %s",
                cudaGetErrorString(err));
            return false;
        }

        // 标记为未映射
        for (int i = 0; i < count; ++i) {
            for (auto& [key, info] : impl_->registeredResources) {
                if (info.resource == resources[i]) {
                    info.mapped = false;
                    break;
                }
            }
        }

        impl_->activeMappings--;

        return true;
    }

    // ============================================================================
    // 获取映射指针
    // ============================================================================

    bool CudaInterop::getMappedPointer(void* cudaResource,
        void** outDevicePtr,
        size_t* outSize)
    {
        if (!impl_->initialized || !cudaResource) return false;

        cudaGraphicsResource_t resource =
            static_cast<cudaGraphicsResource_t>(cudaResource);

        cudaError_t err = cudaGraphicsResourceGetMappedPointer(
            outDevicePtr,
            outSize,
            resource);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaGraphicsResourceGetMappedPointer failed: %s",
                cudaGetErrorString(err));
            return false;
        }

        return true;
    }

    // ============================================================================
    // 拷贝
    // ============================================================================

    bool CudaInterop::copyTextureToBuffer(void* cudaResource,
        void* cudaBuffer,
        size_t bufferSize,
        void* cudaStream)
    {
        if (!impl_->initialized || !cudaResource || !cudaBuffer) return false;

        // 获取映射指针
        void* devicePtr = nullptr;
        size_t size = 0;

        if (!getMappedPointer(cudaResource, &devicePtr, &size)) {
            return false;
        }

        if (size > bufferSize) {
            LOG_ERROR("Buffer too small: need %zu, have %zu", size, bufferSize);
            return false;
        }

        cudaStream_t stream = static_cast<cudaStream_t>(cudaStream);

        cudaError_t err = cudaMemcpyAsync(
            cudaBuffer,
            devicePtr,
            size,
            cudaMemcpyDeviceToDevice,
            stream);

        return err == cudaSuccess;
    }

    // ============================================================================
    // 支持检测
    // ============================================================================

    bool CudaInterop::isSupported() {
        int deviceCount = 0;
        cudaError_t err = cudaGetDeviceCount(&deviceCount);

        if (err != cudaSuccess || deviceCount == 0) return false;

        for (int i = 0; i < deviceCount; ++i) {
            cudaDeviceProp prop;
            if (cudaGetDeviceProperties(&prop, i) != cudaSuccess) continue;

            // 检查 D3D11 互操作支持
            if (prop.major >= 3) {
                // 现代 GPU 都支持
                return true;
            }
        }

        return false;
    }

} // namespace Lingjing
#endif