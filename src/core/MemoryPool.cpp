#include "core/MemoryPool.h"
#include "core/Logger.h"

#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace Lingjing {

    // ============================================================================
    // 单例
    // ============================================================================

    MemoryPool& MemoryPool::instance() {
        static MemoryPool inst;
        return inst;
    }

    MemoryPool::~MemoryPool() {
        cleanup();
    }

    // ============================================================================
    // 对齐分配
    // ============================================================================

    void* MemoryPool::allocate(size_t size, size_t alignment) {
        if (size == 0) return nullptr;

        // 保证 alignment 是 2 的幂
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            alignment = 64;
        }

        void* ptr = nullptr;

#ifdef _WIN32
        ptr = _aligned_malloc(size, alignment);
#else
        if (posix_memalign(&ptr, alignment, size) != 0) {
            ptr = nullptr;
        }
#endif

        if (!ptr) {
            LOG_ERROR("Memory allocation failed: %zu bytes, alignment %zu",
                size, alignment);
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            allocations_[ptr] = { size, alignment };

            stats_.totalAllocated += size;
            stats_.currentUsage += size;
            stats_.allocationCount++;

            uint64_t current = stats_.currentUsage.load();
            uint64_t peak = stats_.peakUsage.load();
            while (current > peak) {
                if (stats_.peakUsage.compare_exchange_weak(peak, current)) {
                    break;
                }
            }
        }

        return ptr;
    }

    void MemoryPool::deallocate(void* ptr) {
        if (!ptr) return;

        size_t size = 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = allocations_.find(ptr);
            if (it == allocations_.end()) {
                LOG_WARN("Attempted to deallocate unknown pointer: %p", ptr);
                return;
            }

            size = it->second.size;
            allocations_.erase(it);

            stats_.totalFreed += size;
            stats_.currentUsage -= size;
            stats_.freeCount++;
        }

#ifdef _WIN32
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    void MemoryPool::cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);

        // 注意：如果还有未释放的分配，这里会泄漏
        // 正常使用下应该已经全部释放
        if (!allocations_.empty()) {
            LOG_WARN("MemoryPool cleanup with %zu outstanding allocations",
                allocations_.size());

            for (const auto& [ptr, info] : allocations_) {
#ifdef _WIN32
                _aligned_free(ptr);
#else
                free(ptr);
#endif
            }

            allocations_.clear();
        }
    }

} // namespace Lingjing