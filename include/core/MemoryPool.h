#pragma once

#include "Types.h"
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>

namespace Lingjing {

    // ============================================================================
    // CPU 内存池
    // ============================================================================

    class MemoryPool {
    public:
        static MemoryPool& instance();

        // 分配对齐内存
        void* allocate(size_t size, size_t alignment = 64);
        void deallocate(void* ptr);

        // 统计
        struct Stats {
            std::atomic<uint64_t> totalAllocated{ 0 };
            std::atomic<uint64_t> totalFreed{ 0 };
            std::atomic<uint64_t> currentUsage{ 0 };
            std::atomic<uint64_t> peakUsage{ 0 };
            std::atomic<uint64_t> allocationCount{ 0 };
            std::atomic<uint64_t> freeCount{ 0 };
        };

        const Stats& stats() const { return stats_; }

        // 清理所有缓存
        void cleanup();

    private:
        MemoryPool() = default;
        ~MemoryPool();

        MemoryPool(const MemoryPool&) = delete;
        MemoryPool& operator=(const MemoryPool&) = delete;

        struct AllocationInfo {
            size_t size;
            size_t alignment;
        };

        std::mutex mutex_;
        std::unordered_map<void*, AllocationInfo> allocations_;
        Stats stats_;
    };

    // ============================================================================
    // RAII 内存包装
    // ============================================================================

    template<typename T>
    class PooledArray {
    public:
        PooledArray() = default;

        explicit PooledArray(size_t count)
            : count_(count) {
            if (count > 0) {
                data_ = static_cast<T*>(
                    MemoryPool::instance().allocate(count * sizeof(T)));
            }
        }

        ~PooledArray() {
            if (data_) {
                MemoryPool::instance().deallocate(data_);
            }
        }

        PooledArray(const PooledArray&) = delete;
        PooledArray& operator=(const PooledArray&) = delete;

        PooledArray(PooledArray&& other) noexcept
            : data_(other.data_), count_(other.count_) {
            other.data_ = nullptr;
            other.count_ = 0;
        }

        PooledArray& operator=(PooledArray&& other) noexcept {
            if (this != &other) {
                if (data_) {
                    MemoryPool::instance().deallocate(data_);
                }
                data_ = other.data_;
                count_ = other.count_;
                other.data_ = nullptr;
                other.count_ = 0;
            }
            return *this;
        }

        T* data() { return data_; }
        const T* data() const { return data_; }

        size_t count() const { return count_; }
        size_t sizeBytes() const { return count_ * sizeof(T); }

        bool empty() const { return count_ == 0 || data_ == nullptr; }

        T& operator[](size_t index) { return data_[index]; }
        const T& operator[](size_t index) const { return data_[index]; }

        void resize(size_t newCount) {
            if (newCount == count_) return;

            if (data_) {
                MemoryPool::instance().deallocate(data_);
            }

            count_ = newCount;
            if (count_ > 0) {
                data_ = static_cast<T*>(
                    MemoryPool::instance().allocate(count_ * sizeof(T)));
            }
            else {
                data_ = nullptr;
            }
        }

    private:
        T* data_ = nullptr;
        size_t count_ = 0;
    };

} // namespace Lingjing