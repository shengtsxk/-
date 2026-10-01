#pragma once
#ifdef LJ_NVIDIA

#include "core/Types.h"
#include "core/Error.h"

#include <memory>

namespace Lingjing {

    // ============================================================================
    // CUDA-D3D11 互操作
    // ============================================================================

    class CudaInterop {
    public:
        CudaInterop();
        ~CudaInterop();

        // 初始化（需要在 CUDA 上下文初始化后调用）
        bool initialize(void* d3d11Device);

        void shutdown();

        // 注册 D3D11 纹理到 CUDA
        bool registerTexture(void* d3d11Texture,
            void** outCudaResource);

        // 注销
        bool unregisterTexture(void* cudaResource);

        // 映射纹理以便 CUDA 访问
        bool mapResources(void** cudaResources,
            int count,
            void* cudaStream);

        // 取消映射
        bool unmapResources(void** cudaResources,
            int count,
            void* cudaStream);

        // 获取映射后的设备指针
        bool getMappedPointer(void* cudaResource,
            void** outDevicePtr,
            size_t* outSize);

        // 拷贝 D3D11 纹理到 CUDA 缓冲
        bool copyTextureToBuffer(void* cudaResource,
            void* cudaBuffer,
            size_t bufferSize,
            void* cudaStream);

        // 检查设备是否支持互操作
        static bool isSupported();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing
#endif