#pragma once

#include "core/Types.h"

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 游戏指纹
        // ============================================================================

        struct GameFingerprint {
            // 静态部分
            uint64_t processHash = 0;         // 进程路径 + 版本哈希
            uint64_t windowClassHash = 0;     // 窗口类名哈希
            uint64_t gpuHash = 0;             // GPU 厂商 + 型号哈希
            uint64_t driverHash = 0;          // 驱动版本哈希

            // 动态部分
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t refreshRate = 0;
            uint32_t swapchainFormat = 0;

            // 综合指纹
            uint64_t combinedHash = 0;

            // 其他信息
            std::wstring processPath;
            std::wstring windowClassName;
            std::wstring windowTitle;

            bool valid = false;

            // ====================================================================
            // 哈希算法
            // ====================================================================

            static uint64_t fnv1a(const void* data, size_t size,
                uint64_t seed = 14695981039346656037ULL);

            static uint64_t hashString(const std::string& str);
            static uint64_t hashWString(const std::wstring& str);

            // ====================================================================
            // 采集
            // ====================================================================

            // 从 HWND 采集指纹
            static GameFingerprint capture(void* hwnd);

            // 从进程路径采集
            static GameFingerprint captureFromProcess(const std::wstring& path,
                uint32_t width,
                uint32_t height);

            // 组合哈希
            void computeCombinedHash();

            // 序列化
            std::string toString() const;

            // 比较
            bool matches(const GameFingerprint& other) const;
            float similarity(const GameFingerprint& other) const;
        };

    } // namespace Learning
} // namespace Lingjing