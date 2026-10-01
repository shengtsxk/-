#pragma once
#ifdef LJ_INTEL

#include "core/Types.h"
#include "core/Error.h"

#include <memory>

namespace Lingjing {

    class SyclInterop {
    public:
        SyclInterop();
        ~SyclInterop();

        bool initialize(void* d3d11Device,
            void* d3d11Context,
            void* syclQueue);

        void shutdown();

        // 通过共享句柄导入 D3D11 纹理到 SYCL
        // 需要 D3D11 纹理创建时使用 D3D11_RESOURCE_MISC_SHARED
        bool importTextureFromSharedHandle(void* sharedHandle,
            uint32_t width,
            uint32_t height,
            TextureFormat format,
            void** outSyclImage);

        // 释放导入的资源
        void releaseImage(void* syclImage);

        // 拷贝 SYCL 图像到 D3D11 纹理
        bool copyImageToD3D11(void* syclImage,
            void* d3d11Texture);

        // 检查支持
        static bool isSupported();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing
#endif