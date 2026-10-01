#ifdef LJ_NVIDIA

#include "gpu/CudaContext.h"
#include "core/Logger.h"

#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // CUDA 错误检查宏
    // ============================================================================

#define LJ_CUDA_CHECK(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            LOG_ERROR("CUDA error at %s:%d: %s (code %d)", \
                      __FILE__, __LINE__, \
                      cudaGetErrorString(err), static_cast<int>(err)); \
            return false; \
        } \
    } while (0)

#define LJ_CUDA_CHECK_VOID(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            LOG_ERROR("CUDA error at %s:%d: %s (code %d)", \
                      __FILE__, __LINE__, \
                      cudaGetErrorString(err), static_cast<int>(err)); \
            return; \
        } \
    } while (0)

// ============================================================================
// CUDA 流实现
// ============================================================================

    class CudaStream : public IGpuStream {
    public:
        CudaStream(cudaStream_t stream, StreamPriority priority)
            : stream_(stream), priority_(priority) {
        }

        ~CudaStream() override {
            if (stream_) {
                cudaStreamDestroy(stream_);
                stream_ = nullptr;
            }
        }

        void synchronize() override {
            if (stream_) {
                cudaStreamSynchronize(stream_);
            }
        }

        bool isIdle() const override {
            if (!stream_) return true;
            cudaError_t err = cudaStreamQuery(stream_);
            return err == cudaSuccess;
        }

        void waitEvent(const IGpuEvent& event) override;

        void recordEvent(IGpuEvent& event) override;

        StreamPriority priority() const override { return priority_; }

        void* nativeHandle() const override {
            return reinterpret_cast<void*>(stream_);
        }

        cudaStream_t cudaStream() const { return stream_; }

    private:
        cudaStream_t stream_ = nullptr;
        StreamPriority priority_;
    };

    // ============================================================================
    // CUDA 事件实现
    // ============================================================================

    class CudaEvent : public IGpuEvent {
    public:
        CudaEvent(bool withTiming) {
            unsigned int flags = withTiming ? cudaEventDefault : cudaEventDisableTiming;
            cudaEventCreateWithFlags(&event_, flags);
        }

        ~CudaEvent() override {
            if (event_) {
                cudaEventDestroy(event_);
                event_ = nullptr;
            }
        }

        void record() override {
            // 记录到默认流
            cudaEventRecord(event_, 0);
        }

        void recordOn(cudaStream_t stream) {
            cudaEventRecord(event_, stream);
        }

        bool synchronize(uint32_t timeoutMs) override {
            if (timeoutMs == 0xFFFFFFFF) {
                cudaError_t err = cudaEventSynchronize(event_);
                return err == cudaSuccess;
            }

            // 带超时的轮询
            auto start = std::chrono::steady_clock::now();
            while (true) {
                cudaError_t err = cudaEventQuery(event_);
                if (err == cudaSuccess) return true;
                if (err != cudaErrorNotReady) return false;

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();

                if (elapsed >= static_cast<int64_t>(timeoutMs)) {
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        float elapsedMs(const IGpuEvent& other) const override {
            const auto* otherEvent = dynamic_cast<const CudaEvent*>(&other);
            if (!otherEvent) return 0.0f;

            float ms = 0.0f;
            cudaEventElapsedTime(&ms, event_, otherEvent->event_);
            return ms;
        }

        bool isReady() const override {
            return cudaEventQuery(event_) == cudaSuccess;
        }

        cudaEvent_t cudaEvent() const { return event_; }

    private:
        cudaEvent_t event_ = nullptr;
    };

    // ============================================================================
    // CudaStream 延迟实现
    // ============================================================================

    void CudaStream::waitEvent(const IGpuEvent& event) {
        const auto* cudaEvent = dynamic_cast<const CudaEvent*>(&event);
        if (cudaEvent && stream_) {
            cudaStreamWaitEvent(stream_, cudaEvent->cudaEvent(), 0);
        }
    }

    void CudaStream::recordEvent(IGpuEvent& event) {
        auto* cudaEvent = dynamic_cast<CudaEvent*>(&event);
        if (cudaEvent && stream_) {
            cudaEvent->recordOn(stream_);
        }
    }

    // ============================================================================
    // CUDA 缓冲实现
    // ============================================================================

    class CudaBuffer : public IGpuBuffer {
    public:
        CudaBuffer(size_t size, bool pinned)
            : size_(size), pinned_(pinned) {

            if (pinned) {
                cudaError_t err = cudaMallocHost(&hostPtr_, size);
                if (err != cudaSuccess) {
                    LOG_ERROR("cudaMallocHost failed: %s",
                        cudaGetErrorString(err));
                    hostPtr_ = nullptr;
                }
                devicePtr_ = hostPtr_;  // 固定内存可直接被设备访问
            }
            else {
                cudaError_t err = cudaMalloc(&devicePtr_, size);
                if (err != cudaSuccess) {
                    LOG_ERROR("cudaMalloc failed (%zu bytes): %s",
                        size, cudaGetErrorString(err));
                    devicePtr_ = nullptr;
                }
            }
        }

        ~CudaBuffer() override {
            if (pinned_ && hostPtr_) {
                cudaFreeHost(hostPtr_);
            }
            else if (devicePtr_) {
                cudaFree(devicePtr_);
            }
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

            cudaError_t err;
            if (pinned_) {
                std::memcpy(static_cast<uint8_t*>(hostPtr_) + offset, src, bytes);
                err = cudaSuccess;
            }
            else {
                err = cudaMemcpy(
                    static_cast<uint8_t*>(devicePtr_) + offset,
                    src, bytes,
                    cudaMemcpyDeviceToDevice);
            }

            return err == cudaSuccess;
        }

        bool copyTo(void* dst, size_t bytes, size_t offset) const override {
            if (!devicePtr_ || !dst) return false;
            if (offset + bytes > size_) return false;

            cudaError_t err;
            if (pinned_) {
                std::memcpy(dst, static_cast<const uint8_t*>(hostPtr_) + offset, bytes);
                err = cudaSuccess;
            }
            else {
                err = cudaMemcpy(
                    dst,
                    static_cast<const uint8_t*>(devicePtr_) + offset,
                    bytes,
                    cudaMemcpyDeviceToDevice);
            }

            return err == cudaSuccess;
        }

        bool copyFromAsync(const void* src, size_t bytes,
            IGpuStream& stream, size_t offset) override {
            if (!devicePtr_ || !src) return false;
            if (offset + bytes > size_) return false;

            auto* cudaStream = dynamic_cast<CudaStream*>(&stream);
            if (!cudaStream) return false;

            cudaError_t err = cudaMemcpyAsync(
                static_cast<uint8_t*>(devicePtr_) + offset,
                src, bytes,
                cudaMemcpyDeviceToDevice,
                cudaStream->cudaStream());

            return err == cudaSuccess;
        }

        bool copyToAsync(void* dst, size_t bytes,
            IGpuStream& stream, size_t offset) const override {
            if (!devicePtr_ || !dst) return false;
            if (offset + bytes > size_) return false;

            auto* cudaStream = dynamic_cast<CudaStream*>(&stream);
            if (!cudaStream) return false;

            cudaError_t err = cudaMemcpyAsync(
                dst,
                static_cast<const uint8_t*>(devicePtr_) + offset,
                bytes,
                cudaMemcpyDeviceToDevice,
                cudaStream->cudaStream());

            return err == cudaSuccess;
        }

        void memset(int value) override {
            if (devicePtr_) {
                cudaMemset(devicePtr_, value, size_);
            }
        }

    private:
        void* devicePtr_ = nullptr;
        void* hostPtr_ = nullptr;
        size_t size_ = 0;
        bool pinned_ = false;
    };

    // ============================================================================
    // CUDA 纹理实现
    // ============================================================================

    class CudaTexture : public IGpuTexture {
    public:
        CudaTexture(uint32_t width, uint32_t height, TextureFormat format)
            : width_(width), height_(height), format_(format) {

            size_t bytesPerPixel = getBytesPerPixel(format);
            rowPitch_ = width * bytesPerPixel;
            sizeBytes_ = rowPitch_ * height;

            cudaError_t err = cudaMalloc(&devicePtr_, sizeBytes_);
            if (err != cudaSuccess) {
                LOG_ERROR("cudaMalloc texture failed: %s",
                    cudaGetErrorString(err));
                devicePtr_ = nullptr;
            }
        }

        ~CudaTexture() override {
            if (devicePtr_) {
                cudaFree(devicePtr_);
                devicePtr_ = nullptr;
            }
        }

        uint32_t width() const override { return width_; }
        uint32_t height() const override { return height_; }
        uint32_t depth() const override { return 1; }
        TextureFormat format() const override { return format_; }
        size_t rowPitch() const override { return rowPitch_; }
        size_t sizeBytes() const override { return sizeBytes_; }

        void* devicePtr() override { return devicePtr_; }
        const void* devicePtr() const override { return devicePtr_; }

        void* nativeHandle() const override {
            return devicePtr_;  // CUDA 直接指针
        }

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

        uint32_t width_ = 0;
        uint32_t height_ = 0;
        TextureFormat format_ = TextureFormat::Unknown;
        size_t rowPitch_ = 0;
        size_t sizeBytes_ = 0;
        void* devicePtr_ = nullptr;
    };

    // ============================================================================
    // CudaContext::Impl
    // ============================================================================

    struct CudaContext::Impl {
        bool cudaInitialized = false;
        bool d3d11InteropEnabled = false;
        std::unique_ptr<CudaStream> defaultStream;
        std::mutex mutex;
        std::atomic<uint32_t> streamCounter{ 0 };
    };

    // ============================================================================
    // CudaContext 实现
    // ============================================================================

    CudaContext::CudaContext()
        : impl_(std::make_unique<Impl>()) {
    }

    CudaContext::~CudaContext() {
        shutdown();
    }

    bool CudaContext::initialize(const GpuInfo& gpu) {
        if (valid_) return true;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        info_ = gpu;

        // 初始化 CUDA 运行时
        cudaError_t err = cudaFree(0);
        if (err != cudaSuccess) {
            LOG_ERROR("CUDA runtime initialization failed: %s",
                cudaGetErrorString(err));
            return false;
        }
        impl_->cudaInitialized = true;

        // 获取设备数量
        int deviceCount = 0;
        cudaGetDeviceCount(&deviceCount);

        if (deviceCount == 0) {
            LOG_ERROR("No CUDA devices found");
            return false;
        }

        // 选择匹配的 CUDA 设备
        int selectedDevice = -1;
        for (int i = 0; i < deviceCount; ++i) {
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, i);

            if (prop.deviceID == static_cast<int>(gpu.deviceId) ||
                std::string(prop.name).find(gpu.name) != std::string::npos) {
                selectedDevice = i;
                break;
            }
        }

        if (selectedDevice < 0) {
            // 未找到精确匹配，使用默认设备（0）
            selectedDevice = 0;
            LOG_WARN("No exact CUDA device match for '%s', using device 0",
                gpu.name.c_str());
        }

        deviceIndex_ = selectedDevice;

        err = cudaSetDevice(deviceIndex_);
        if (err != cudaSuccess) {
            LOG_ERROR("cudaSetDevice(%d) failed: %s",
                deviceIndex_, cudaGetErrorString(err));
            return false;
        }

        // 补充设备信息
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, deviceIndex_);

        info_.computeUnits = prop.multiProcessorCount * 128;  // 近似
        info_.maxClockMhz = prop.clockRate / 1000;
        info_.dedicatedVramBytes = prop.totalGlobalMem;

        LOG_INFO("CUDA context: device %d '%s' CC %d.%d",
            deviceIndex_, prop.name,
            prop.major, prop.minor);

        // 创建默认流
        err = cudaStreamCreateWithFlags(&defaultStream_,
            cudaStreamNonBlocking);
        if (err != cudaSuccess) {
            LOG_ERROR("cudaStreamCreate failed: %s", cudaGetErrorString(err));
            return false;
        }

        impl_->defaultStream = std::make_unique<CudaStream>(
            defaultStream_, StreamPriority::Normal);

        valid_ = true;
        return true;
    }

    void CudaContext::shutdown() {
        if (!impl_) return;

        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (defaultStream_) {
            cudaStreamSynchronize(defaultStream_);
            cudaStreamDestroy(defaultStream_);
            defaultStream_ = nullptr;
        }

        impl_->defaultStream.reset();

        if (impl_->d3d11InteropEnabled) {
            cudaDeviceReset();
        }

        valid_ = false;
    }

    bool CudaContext::isValid() const {
        return valid_;
    }

    const GpuInfo& CudaContext::info() const {
        return info_;
    }

    GpuVendor CudaContext::vendor() const {
        return GpuVendor::NVIDIA;
    }

    const char* CudaContext::backendName() const {
        return "CUDA";
    }

    std::unique_ptr<IGpuStream> CudaContext::createStream(
        StreamPriority priority)
    {
        if (!valid_) return nullptr;

        unsigned int flags = cudaStreamNonBlocking;

        int cudaPriority = 0;
        switch (priority) {
        case StreamPriority::Low:      cudaPriority = 0; break;
        case StreamPriority::Normal:   cudaPriority = 0; break;
        case StreamPriority::High:     cudaPriority = -1; break;
        case StreamPriority::Realtime: cudaPriority = -2; break;
        }

        cudaStream_t stream = nullptr;
        cudaError_t err = cudaStreamCreateWithPriority(&stream, flags,
            cudaPriority);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaStreamCreateWithPriority failed: %s",
                cudaGetErrorString(err));
            return nullptr;
        }

        return std::make_unique<CudaStream>(stream, priority);
    }

    void CudaContext::synchronizeAll() {
        if (valid_) {
            cudaDeviceSynchronize();
        }
    }

    std::unique_ptr<IGpuEvent> CudaContext::createEvent() {
        if (!valid_) return nullptr;
        return std::make_unique<CudaEvent>(true);
    }

    std::unique_ptr<IGpuBuffer> CudaContext::createBuffer(
        const GpuBufferDesc& desc)
    {
        if (!valid_ || desc.sizeBytes == 0) return nullptr;

        auto buffer = std::make_unique<CudaBuffer>(desc.sizeBytes, desc.pinned);

        if (desc.zeroInitialize && buffer->devicePtr()) {
            buffer->memset(0);
        }

        if (desc.debugName) {
            // CUDA 3.0+ 支持命名资源
            // cudaStreamAddCallback 等
        }

        return buffer;
    }

    std::unique_ptr<IGpuBuffer> CudaContext::createBufferFromHost(
        const void* hostData,
        size_t sizeBytes,
        BufferUsage usage)
    {
        if (!valid_ || !hostData || sizeBytes == 0) return nullptr;

        auto buffer = std::make_unique<CudaBuffer>(sizeBytes, false);

        if (!buffer->devicePtr()) return nullptr;

        cudaError_t err = cudaMemcpy(buffer->devicePtr(), hostData,
            sizeBytes, cudaMemcpyHostToDevice);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaMemcpy H2D failed: %s", cudaGetErrorString(err));
            return nullptr;
        }

        return buffer;
    }

    std::unique_ptr<IGpuTexture> CudaContext::createTexture(
        uint32_t width,
        uint32_t height,
        TextureFormat format)
    {
        if (!valid_ || width == 0 || height == 0) return nullptr;

        return std::make_unique<CudaTexture>(width, height, format);
    }

    CudaContext::MemoryStats CudaContext::memoryStats() const {
        MemoryStats stats;

        if (!valid_) return stats;

        size_t freeBytes = 0;
        size_t totalBytes = 0;

        cudaError_t err = cudaMemGetInfo(&freeBytes, &totalBytes);

        if (err == cudaSuccess) {
            stats.totalBytes = totalBytes;
            stats.freeBytes = freeBytes;
            stats.usedBytes = totalBytes - freeBytes;
            stats.utilization = (totalBytes > 0) ?
                (static_cast<float>(stats.usedBytes) /
                    static_cast<float>(totalBytes)) : 0.0f;
        }

        return stats;
    }

    IGpuStream& CudaContext::defaultStream() {
        return *impl_->defaultStream;
    }

    void* CudaContext::nativeDevice() const {
        return reinterpret_cast<void*>(static_cast<intptr_t>(deviceIndex_));
    }

    void* CudaContext::nativeContext() const {
        // 返回主 CUDA 上下文
        CUcontext ctx = nullptr;
        // 注意：需要包含 cuda.h 才能调用 cuCtxGetCurrent
        // 这里简化返回 null，实际使用需要 cuda.h
        return ctx;
    }

    bool CudaContext::createFromD3D11Device(void* d3d11Device) {
        if (!d3d11Device) return false;

        if (!valid_) return false;

        // 注册 D3D11 设备到 CUDA
        cudaError_t err = cudaD3D11SetDirect3DDevice(d3d11Device);

        if (err != cudaSuccess) {
            LOG_ERROR("cudaD3D11SetDirect3DDevice failed: %s",
                cudaGetErrorString(err));
            return false;
        }

        impl_->d3d11InteropEnabled = true;

        LOG_INFO("CUDA-D3D11 interop enabled");

        return true;
    }

    // ============================================================================
    // 工厂
    // ============================================================================

    std::unique_ptr<IGpuContext> createCudaContext() {
        return std::make_unique<CudaContext>();
    }

} // namespace Lingjing
#endif