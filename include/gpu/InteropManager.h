#pragma once

#include "core/Types.h"
#include "core/DeviceCaps.h"
#include "gpu/IGpuContext.h"

#include <memory>

namespace Lingjing {

    // ============================================================================
    // 互操作管理器
    // 统一管理 D3D11 纹理到 GPU 计算上下文的共享
    // ============================================================================

    class InteropManager {
    public:
        InteropManager();
        ~InteropManager();

        // 初始化
        bool initialize(IGpuContext* gpuContext,
            void* d3d11Device,
            void* d3d11Context);

        void shutdown();

        // ====================================================================
        // 纹理共享
        // ====================================================================

        // 注册 D3D11 纹理，返回互操作句柄
        void* registerTexture(void* d3d11Texture,
            uint32_t width,
            uint32_t height,
            TextureFormat format);

        // 注销
        void unregisterTexture(void* interopHandle);

        // 获取映射后的设备指针
        void* mapTexture(void* interopHandle, IGpuStream& stream);

        // 取消映射
        bool unmapTexture(void* interopHandle, IGpuStream& stream);

        // ====================================================================
        // 查询
        // ====================================================================

        bool isInitialized() const { return initialized_; }
        GpuVendor vendor() const;

        size_t registeredTextureCount() const;
        size_t activeMappingCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        bool initialized_ = false;
    };

} // namespace Lingjing