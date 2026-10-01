#pragma once

#include "core/Types.h"
#include "core/DeviceCaps.h"
#include "core/Error.h"

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Lingjing {

    // ============================================================================
    // GPU 流优先级
    // ============================================================================

    enum class StreamPriority : uint32_t {
        Low = 0,
        Normal = 1,
        High = 2,
        Realtime = 3,
    };

    // ============================================================================
    // GPU 缓冲用途
    // ============================================================================

    enum class BufferUsage : uint32_t {
        Default = 0,
        Staging,
        ReadOnly,
        WriteOnly,
        ReadWrite,
        Uniform,
        Texture,
        Constant,
        Dynamic,
    };

    // ============================================================================
    // GPU 缓冲描述
    // ============================================================================

    struct GpuBufferDesc {
        size_t sizeBytes = 0;
        BufferUsage usage = BufferUsage::Default;
        bool pinned = false;              // 固定内存（主机可访问）
        bool zeroInitialize = false;      // 分配后清零
        const char* debugName = nullptr;
    };

    // ============================================================================
    // GPU 事件
    // ============================================================================

    class IGpuEvent {
    public:
        virtual ~IGpuEvent() = default;

        virtual void record() = 0;
        virtual bool synchronize(uint32_t timeoutMs = 0xFFFFFFFF) = 0;
        virtual float elapsedMs(const IGpuEvent& other) const = 0;
        virtual bool isReady() const = 0;
    };

    // ============================================================================
    // GPU 流
    // ============================================================================

    class IGpuStream {
    public:
        virtual ~IGpuStream() = default;

        virtual void synchronize() = 0;
        virtual bool isIdle() const = 0;

        virtual void waitEvent(const IGpuEvent& event) = 0;
        virtual void recordEvent(IGpuEvent& event) = 0;

        virtual StreamPriority priority() const = 0;
        virtual void* nativeHandle() const = 0;
    };

    // ============================================================================
    // GPU 缓冲
    // ============================================================================

    class IGpuBuffer {
    public:
        virtual ~IGpuBuffer() = default;

        virtual void* devicePtr() = 0;
        virtual const void* devicePtr() const = 0;

        virtual void* hostPtr() = 0;
        virtual const void* hostPtr() const = 0;

        virtual size_t sizeBytes() const = 0;
        virtual bool isPinned() const = 0;

        virtual bool copyFrom(const void* src, size_t bytes,
            size_t offset = 0) = 0;
        virtual bool copyTo(void* dst, size_t bytes,
            size_t offset = 0) const = 0;

        virtual bool copyFromAsync(const void* src, size_t bytes,
            IGpuStream& stream,
            size_t offset = 0) = 0;
        virtual bool copyToAsync(void* dst, size_t bytes,
            IGpuStream& stream,
            size_t offset = 0) const = 0;

        virtual void memset(int value) = 0;
    };

    // ============================================================================
    // GPU 纹理
    // ============================================================================

    class IGpuTexture {
    public:
        virtual ~IGpuTexture() = default;

        virtual uint32_t width() const = 0;
        virtual uint32_t height() const = 0;
        virtual uint32_t depth() const = 0;
        virtual TextureFormat format() const = 0;
        virtual size_t rowPitch() const = 0;
        virtual size_t sizeBytes() const = 0;

        virtual void* devicePtr() = 0;
        virtual const void* devicePtr() const = 0;

        virtual void* nativeHandle() const = 0;
    };

    // ============================================================================
    // GPU 上下文接口
    // ============================================================================

    class IGpuContext {
    public:
        virtual ~IGpuContext() = default;

        // ====================================================================
        // 生命周期
        // ====================================================================

        virtual bool initialize(const GpuInfo& gpu) = 0;
        virtual void shutdown() = 0;

        // ====================================================================
        // 查询
        // ====================================================================

        virtual bool isValid() const = 0;
        virtual const GpuInfo& info() const = 0;
        virtual GpuVendor vendor() const = 0;
        virtual const char* backendName() const = 0;

        // ====================================================================
        // 流管理
        // ====================================================================

        virtual std::unique_ptr<IGpuStream> createStream(
            StreamPriority priority = StreamPriority::Normal) = 0;

        virtual void synchronizeAll() = 0;

        // ====================================================================
        // 事件管理
        // ====================================================================

        virtual std::unique_ptr<IGpuEvent> createEvent() = 0;

        // ====================================================================
        // 缓冲管理
        // ====================================================================

        virtual std::unique_ptr<IGpuBuffer> createBuffer(
            const GpuBufferDesc& desc) = 0;

        virtual std::unique_ptr<IGpuBuffer> createBufferFromHost(
            const void* hostData,
            size_t sizeBytes,
            BufferUsage usage = BufferUsage::Default) = 0;

        // ====================================================================
        // 纹理管理
        // ====================================================================

        virtual std::unique_ptr<IGpuTexture> createTexture(
            uint32_t width,
            uint32_t height,
            TextureFormat format) = 0;

        // ====================================================================
        // 内存统计
        // ====================================================================

        struct MemoryStats {
            size_t totalBytes = 0;
            size_t freeBytes = 0;
            size_t usedBytes = 0;
            size_t reservedBytes = 0;
            float utilization = 0.0f;
        };

        virtual MemoryStats memoryStats() const = 0;

        // ====================================================================
        // 同步原语
        // ====================================================================

        // 获取默认流
        virtual IGpuStream& defaultStream() = 0;

        // 获取当前设备原始句柄
        virtual void* nativeDevice() const = 0;
        virtual void* nativeContext() const = 0;
    };

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IGpuContext> createCudaContext();
    std::unique_ptr<IGpuContext> createSyclContext();

} // namespace Lingjing