#pragma once
#ifdef LJ_NVIDIA

#include "gpu/IGpuContext.h"

#include <cuda_runtime.h>
#include <memory>

namespace Lingjing {

    class CudaContext : public IGpuContext {
    public:
        CudaContext();
        ~CudaContext() override;

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
        // CUDA 特有
        // ====================================================================

        int deviceIndex() const { return deviceIndex_; }
        cudaStream_t defaultCudaStream() const { return defaultStream_; }

        // 从 D3D11 设备创建 CUDA 上下文
        bool createFromD3D11Device(void* d3d11Device);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        int deviceIndex_ = -1;
        cudaStream_t defaultStream_ = nullptr;
        GpuInfo info_;
        bool valid_ = false;
    };

} // namespace Lingjing
#endif