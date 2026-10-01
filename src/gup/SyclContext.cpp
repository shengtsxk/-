#ifdef LJ_INTEL

#include "gpu/SyclContext.h"
#include "core/Logger.h"

#include <sycl/sycl.hpp>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>

namespace Lingjing {

    // ============================================================================
    // SYCL 流实现
    // ============================================================================

    class SyclStream : public IGpuStream {
    public:
        SyclStream(sycl::queue queue, StreamPriority priority)
            : queue_(std::move(queue)), priority_(priority) {
        }

        ~SyclStream() override {
            try {
                queue_.wait_and_throw();
            }
            catch (...) {}
        }

        void synchronize() override {
            try {
                queue_.wait_and_throw();
            }
            catch (const sycl::exception& e) {
                LOG_ERROR("SYCL synchronize failed: %s", e.what());
            }
        }

        bool isIdle() const override {
            // SYCL 没有直接的 idle 查询
            // 使用 in-order 队列或检查事件
            return true;
        }

        void waitEvent(const IGpuEvent& event) override {
            // 通过 event 同步
            // 简化：SYCL 使用事件依赖
        }

        void recordEvent(IGpuEvent& event) override {
            // SYCL 事件由命令自动记录
        }

        StreamPriority priority() const override { return priority_; }

        void* nativeHandle() const override {
            return const_cast<sycl::queue*>(&queue_);
        }

        sycl::queue& syclQueue() { return queue_; }
        const sycl::queue& syclQueue() const { return queue_; }

    private:
        sycl::queue queue_;
        StreamPriority priority_;
    };

    // ============================================================================
    // SYCL 事件实现
    // ============================================================================

    class SyclEvent : public IGpuEvent {
    public:
        SyclEvent() : event_(sycl::event{}) {}

        void record() override {
            // SYCL 事件在提交命令时自动创建
            // 通过 submit 命令并保存事件
            sycl::queue q;
            event_ = q.submit([&](sycl::handler& h) {});
        }

        bool synchronize(uint32_t timeoutMs) override {
            try {
                if (timeoutMs == 0xFFFFFFFF) {
                    event_.wait();
                    return true;
                }

                auto status = event_.get_info<sycl::info::event::command_execution_status>();
                return status == sycl::info::event_command_status::complete;
            }
            catch (...) {
                return false;
            }
        }

        float elapsedMs(const IGpuEvent& other) const override {
            try {
                auto start = event_.get_profiling_info<
                    sycl::info::event_profiling::command_start>();
                auto end = event_.get_profiling_info<
                    sycl::info::event_profiling::command_end>();
                return static_cast<float>(end - start) / 1e6f;
            }
            catch (...) {
                return 0.0f;
            }
        }

        bool isReady() const override {
            try {
                auto status = event_.get_info<
                    sycl::info::event::command_execution_status>();
                return status == sycl::info::event_command_status::complete;
            }
            catch (...) {
                return false;
            }
        }

        sycl::event& syclEvent() { return event_; }
        const sycl::event& syclEvent() const { return event_; }

    private:
        sycl::event event_;
    };

    // ============================================================================
    // SYCL 缓冲实现
    // ============================================================================

    class SyclBuffer : public IGpuBuffer {
    public:
        SyclBuffer(sycl::queue& queue, size_t size, bool pinned)
            : queue_(queue), size_(size), pinned_(pinned) {

            if (pinned) {
                // 主机固定内存（USM host）
                devicePtr_ = sycl::malloc_host(size, queue_);
                hostPtr_ = devicePtr_;
            }
            else {
                // 设备内存（USM device）
                devicePtr_ = sycl::malloc_device(size, queue_);
                hostPtr_ = nullptr;
            }

            if (!devicePtr_) {
                LOG_ERROR("SYCL memory allocation failed (%zu bytes)", size);
            }
        }

        ~SyclBuffer() override {
            try {
                if (pinned_ && hostPtr_) {
                    sycl::free(hostPtr_, queue_);
                }
                else if (devicePtr_) {
                    sycl::free(devicePtr_, queue_);
                }
            }
            catch (...) {}

            devicePtr_ = nullptr;
            hostPtr_ = nullptr;
        }

        void* devicePtr() override { return devicePtr_; }
        const void* devicePtr() const override { return devicePtr_; }

        void* hostPtr() override { return hostPtr_; }
        const void* hostPtr() const override { return hostPtr_; }

        size_t sizeBytes() const override { return size_; }
        bool isPinned() const override { return pinned_; }

        bool copyFrom(const void* src, size_t bytes, size_t offset) override {
            if (!devicePtr_ || !src) return false;
            if (offset + bytes > size_) return false;

            try {
                if (pinned_) {
                    std::memcpy(static_cast<uint8_t*>(hostPtr_) + offset,
                        src, bytes);
                }
                else {
                    queue_.memcpy(static_cast<uint8_t*>(devicePtr_) + offset,
                        src, bytes);
                    queue_.wait();
                }
                return true;
            }
            catch (const sycl::exception& e) {
                LOG_ERROR("SYCL memcpy failed: %s", e.what());
                return false;
            }
        }

        bool copyTo(void* dst, size_t bytes, size_t offset) const override {
            if (!devicePtr_ || !dst) return false;
            if (offset + bytes > size_) return false;

            try {
                if (pinned_) {
                    std::memcpy(dst,
                        static_cast<const uint8_t*>(hostPtr_) + offset,
                        bytes);
                }
                else {
                    auto& q = const_cast<sycl::queue&>(queue_);
                    q.memcpy(dst,
                        static_cast<const uint8_t*>(devicePtr_) + offset,
                        bytes);
                    q.wait();
                }
                return true;
            }
            catch (const sycl::exception& e) {
                LOG_ERROR("SYCL memcpy failed: %s", e.what());
                return false;
            }
        }

        bool copyFromAsync(const void* src, size_t bytes,
            IGpuStream& stream, size_t offset) override {
            auto* syclStream = dynamic_cast<SyclStream*>(&stream);
            if (!syclStream) return false;

            if (!devicePtr_ || !src) return false;
            if (offset + bytes > size_) return false;

            try {
                syclStream->syclQueue().memcpy(
                    static_cast<uint8_t*>(devicePtr_) + offset,
                    src, bytes);
                return true;
            }
            catch (const sycl::exception& e) {
                LOG_ERROR("SYCL async memcpy failed: %s", e.what());
                return false;
            }
        }

        bool copyToAsync(void* dst, size_t bytes,
            IGpuStream& stream, size_t offset) const override {
            auto* syclStream = dynamic_cast<SyclStream*>(&stream);
            if (!syclStream) return false;

            if (!devicePtr_ || !dst) return false;
            if (offset + bytes > size_) return false;

            try {
                syclStream->syclQueue().memcpy(
                    dst,
                    static_cast<const uint8_t*>(devicePtr_) + offset,
                    bytes);
                return true;
            }
            catch (const sycl::exception& e) {
                LOG_ERROR("SYCL async memcpy failed: %s", e.what());
                return false;
            }
        }

        void memset(int value) override {
            if (!devicePtr_) return;

            try {
                queue_.memset(devicePtr_, value, size_);
                queue_.wait();
            }
            catch (const sycl::exception& e) {
                LOG_ERROR("SYCL memset failed: %s", e.what());
            }
        }

    private:
        sycl::queue& queue_;
        void* devicePtr_ = nullptr;
        void* hostPtr_ = nullptr;
        size_t size_ = 0;
        bool pinned_ = false;
    };

    // ============================================================================
    // SYCL 纹理实现
    // ============================================================================

    class SyclTexture : public IGpuTexture {
    public:
        SyclTexture(sycl::queue& queue,
            uint32_t width, uint32_t height,
            TextureFormat format)
            : queue_(queue), width_(width), height_(height), format_(format) {

            size_t bytesPerPixel = getBytesPerPixel(format);
            rowPitch_ = width * bytesPerPixel;
            sizeBytes_ = rowPitch_ * height;

            devicePtr_ = sycl::malloc_device(sizeBytes_, queue_);

            if (!devicePtr_) {
                LOG_ERROR("SYCL texture allocation failed (%zu bytes)",
                    sizeBytes_);
            }
        }

        ~SyclTexture() override {
            try {
                if (devicePtr_) {
                    sycl::free(devicePtr_, queue_);
                }
            }
            catch (...) {}
            devicePtr_ = nullptr;
        }

        uint32_t width() const override { return width_; }
        uint32_t height() const override { return height_; }
        uint32_t depth() const override { return 1; }
        TextureFormat format() const override { return format_; }
        size_t rowPitch() const override { return rowPitch_; }
        size_t sizeBytes() const override { return sizeBytes_; }

        void* devicePtr() override { return devicePtr_; }
        const void* devicePtr() const override { return devicePtr_; }

        void* nativeHandle() const override { return devicePtr_; }

    private:
        static size_t getBytesPerPixel(TextureFormat fmt) {
            switch (fmt) {
            case TextureFormat::R8_UNORM: return 1;
            case TextureFormat::R8G8_UNORM: return 2;
            case TextureFormat::R8G8B8A8_UNORM: return 4;
            case TextureFormat::B8G8R8A8_UNORM: return 4;
            case TextureFormat::R10G10B10A2_UNORM: return 4;
            case TextureFormat::R16_FLOAT: return 2;
            case TextureFormat::R16G16_FLOAT: return 4;
            case TextureFormat::R16G16B16A16_FLOAT: return 8;
            case TextureFormat::R32_FLOAT: return 4;
            case TextureFormat::R32G32_FLOAT: return 8;
            case TextureFormat::R32G32B32A32_FLOAT: return 16;
            default: return 4;
            }
        }

        sycl::queue& queue_;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        TextureFormat format_ = TextureFormat::Unknown;
        size_t rowPitch_ = 0;
        size_t sizeBytes_ = 0;
        void* devicePtr_ = nullptr;
    };

    // ============================================================================
    // SyclContext::Impl
    // ============================================================================

    struct SyclContext::Impl {
        std::unique_ptr<sycl::device> device;
        std::unique_ptr<sycl::context> context;
        std::unique_ptr<sycl::queue> defaultQueue;
        std::unique_ptr<SyclStream> defaultStream;
        std::mutex mutex;
        bool interopEnabled = false;
    };

    // ============================================================================
    // SyclContext 实现
    // ============================================================================

    SyclContext::SyclContext()
        : impl_(std::make_unique<Impl>()) {
    }

    SyclContext::~SyclContext() {
        shutdown();
    }

    bool SyclContext::initialize(const GpuInfo& gpu) {
        if (valid_) return true;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        info_ = gpu;

        try {
            // 枚举所有 SYCL 设备
            auto devices = sycl::device::get_devices(sycl::info::device_type::gpu);

            if (devices.empty()) {
                LOG_ERROR("No SYCL GPU devices found");
                return false;
            }

            // 选择匹配的设备
            sycl::device* selected = nullptr;

            for (auto& dev : devices) {
                std::string name = dev.get_info<sycl::info::device::name>();

                if (name.find(gpu.name) != std::string::npos ||
                    name.find("Intel") != std::string::npos) {
                    selected = &dev;
                    break;
                }
            }

            if (!selected) {
                selected = &devices[0];
                LOG_WARN("No exact SYCL device match, using default");
            }

            impl_->device = std::make_unique<sycl::device>(*selected);

            // 创建上下文
            impl_->context = std::make_unique<sycl::context>(*impl_->device);

            // 创建默认队列（启用性能分析）
            sycl::property_list props = {
                sycl::property::queue::enable_profiling(),
                sycl::property::queue::in_order()
            };

            impl_->defaultQueue = std::make_unique<sycl::queue>(
                *impl_->context, *impl_->device, props);

            impl_->defaultStream = std::make_unique<SyclStream>(
                *impl_->defaultQueue, StreamPriority::Normal);

            // 获取设备信息
            auto& dev = *impl_->device;

            info_.name = dev.get_info<sycl::info::device::name>();

            if (dev.has(sycl::info::device::max_compute_units)) {
                info_.computeUnits =
                    dev.get_info<sycl::info::device::max_compute_units>();
            }

            if (dev.has(sycl::info::device::max_clock_frequency)) {
                info_.maxClockMhz =
                    dev.get_info<sycl::info::device::max_clock_frequency>();
            }

            if (dev.has(sycl::info::device::global_mem_size)) {
                info_.dedicatedVramBytes =
                    dev.get_info<sycl::info::device::global_mem_size>();
            }

            LOG_INFO("SYCL context: device '%s', CU=%u, VRAM=%llu MB",
                info_.name.c_str(),
                info_.computeUnits,
                info_.dedicatedVramBytes / (1024 * 1024));

            valid_ = true;
            return true;
        }
        catch (const sycl::exception& e) {
            LOG_ERROR("SYCL initialization failed: %s", e.what());
            return false;
        }
    }

    void SyclContext::shutdown() {
        if (!impl_) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (impl_->defaultQueue) {
            try {
                impl_->defaultQueue->wait_and_throw();
            }
            catch (...) {}
        }

        impl_->defaultStream.reset();
        impl_->defaultQueue.reset();
        impl_->context.reset();
        impl_->device.reset();

        valid_ = false;
    }

    bool SyclContext::isValid() const { return valid_; }
    const GpuInfo& SyclContext::info() const { return info_; }
    GpuVendor SyclContext::vendor() const { return GpuVendor::Intel; }
    const char* SyclContext::backendName() const { return "SYCL"; }

    std::unique_ptr<IGpuStream> SyclContext::createStream(
        StreamPriority priority)
    {
        if (!valid_) return nullptr;

        try {
            sycl::property_list props = {
                sycl::property::queue::enable_profiling(),
                sycl::property::queue::in_order()
            };

            sycl::queue queue(*impl_->context, *impl_->device, props);

            return std::make_unique<SyclStream>(std::move(queue), priority);
        }
        catch (const sycl::exception& e) {
            LOG_ERROR("SYCL queue creation failed: %s", e.what());
            return nullptr;
        }
    }

    void SyclContext::synchronizeAll() {
        if (!valid_) return;

        try {
            impl_->defaultQueue->wait_and_throw();
        }
        catch (...) {}
    }

    std::unique_ptr<IGpuEvent> SyclContext::createEvent() {
        if (!valid_) return nullptr;
        return std::make_unique<SyclEvent>();
    }

    std::unique_ptr<IGpuBuffer> SyclContext::createBuffer(
        const GpuBufferDesc& desc)
    {
        if (!valid_ || desc.sizeBytes == 0) return nullptr;

        auto buffer = std::make_unique<SyclBuffer>(
            *impl_->defaultQueue, desc.sizeBytes, desc.pinned);

        if (desc.zeroInitialize) {
            buffer->memset(0);
        }

        return buffer;
    }

    std::unique_ptr<IGpuBuffer> SyclContext::createBufferFromHost(
        const void* hostData,
        size_t sizeBytes,
        BufferUsage usage)
    {
        if (!valid_ || !hostData || sizeBytes == 0) return nullptr;

        auto buffer = std::make_unique<SyclBuffer>(
            *impl_->defaultQueue, sizeBytes, false);

        if (!buffer->devicePtr()) return nullptr;

        if (!buffer->copyFrom(hostData, sizeBytes)) return nullptr;

        return buffer;
    }

    std::unique_ptr<IGpuTexture> SyclContext::createTexture(
        uint32_t width,
        uint32_t height,
        TextureFormat format)
    {
        if (!valid_ || width == 0 || height == 0) return nullptr;

        return std::make_unique<SyclTexture>(
            *impl_->defaultQueue, width, height, format);
    }

    SyclContext::MemoryStats SyclContext::memoryStats() const {
        MemoryStats stats;

        if (!valid_) return stats;

        try {
            auto& dev = *impl_->device;

            if (dev.has(sycl::info::device::global_mem_size)) {
                stats.totalBytes =
                    dev.get_info<sycl::info::device::global_mem_size>();
            }

            // SYCL 没有直接查询空闲内存的接口
            // 使用 approximate
            stats.freeBytes = stats.totalBytes;  // 近似
            stats.usedBytes = 0;
            stats.utilization = 0.0f;
        }
        catch (...) {}

        return stats;
    }

    IGpuStream& SyclContext::defaultStream() {
        return *impl_->defaultStream;
    }

    void* SyclContext::nativeDevice() const {
        if (!impl_->device) return nullptr;
        return reinterpret_cast<void*>(
            impl_->device->get_info<sycl::info::device::vendor_id>());
    }

    void* SyclContext::nativeContext() const {
        return nullptr;
    }

    void* SyclContext::syclQueue() const {
        return reinterpret_cast<void*>(impl_->defaultQueue.get());
    }

    bool SyclContext::createFromD3D11Device(void* d3d11Device) {
        // Intel SYCL 通过 D3D11 互操作需要 oneAPI Level Zero 后端
        // 简化实现：仅标记启用
        if (!d3d11Device || !valid_) return false;

        impl_->interopEnabled = true;
        LOG_INFO("SYCL-D3D11 interop enabled");

        return true;
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IGpuContext> createSyclContext() {
        return std::make_unique<SyclContext>();
    }

} // namespace Lingjing
#endif