#pragma once
#ifdef LJ_INTEL

#include "gpu/IGpuContext.h"

#include <memory>

namespace Lingjing {

    class SyclContext : public IGpuContext {
    public:
        SyclContext();
        ~SyclContext() override;

        bool initialize(const GpuInfo& gpu) override;
        void shutdown() override;

        bool isValid() const override;
        const GpuInfo& info() const override;
        GpuVendor vendor() const override;
        const char* backendName() const override;

        std::unique_ptr<IGpuStream> createStream(
            StreamPriority priority) override;

        void synchronizeAll() override;

        std::unique_ptr<IGpuEvent> createEvent() override;

        std::unique_ptr<IGpuBuffer> createBuffer(
            const GpuBufferDesc& desc) override;

        std::unique_ptr<IGpuBuffer> createBufferFromHost(
            const void* hostData,
            size_t sizeBytes,
            BufferUsage usage) override;

        std::unique_ptr<IGpuTexture> createTexture(
            uint32_t width,
            uint32_t height,
            TextureFormat format) override;

        MemoryStats memoryStats() const override;

        IGpuStream& defaultStream() override;

        void* nativeDevice() const override;
        void* nativeContext() const override;

        // ====================================================================
        // SYCL 特有
        // ====================================================================

        // 获取底层 SYCL 队列
        void* syclQueue() const;

        // 从 D3D11 设备创建
        bool createFromD3D11Device(void* d3d11Device);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        GpuInfo info_;
        bool valid_ = false;
    };

} // namespace Lingjing
#endif