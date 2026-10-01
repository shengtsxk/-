#pragma once

#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace Lingjing {

// ============================================================================
// D3D11 帧插值器（Compute Shader 实现，Windows 自带、免 CUDA/oneAPI）
//
// 管线：BGRA 上传 → 灰度 → 金字塔下采样 → LK 光流（粗到细，fwd/bwd）
//       → 一致性/遮挡 → 多帧 warp + 遮挡感知混合 → BGRA 读回
//
// 性能目标（1080p）：光流 ≤3ms（3060/A570 级）、≤1.5ms（4070 级）、
//       ≤2ms（B580 级）；总延迟（光流+生成+读回）≤10ms。
// ============================================================================

class D3D11FrameInterpolator {
public:
    D3D11FrameInterpolator();
    ~D3D11FrameInterpolator();

    // 创建 D3D11 设备并编译全部内核
    bool initialize();
    void shutdown();
    bool isReady() const { return ready_; }

    // 生成 N 帧插值（frame0/frame1 为 BGRA8，宽高一致）
    // outFrames[i]：第 i 帧中间帧 BGRA8（与输入同尺寸）
    // flowMs/genMs：分阶段计时；quality：估算质量 0~1
    bool generateFrames(
        const uint8_t* frame0,
        const uint8_t* frame1,
        int width,
        int height,
        int numFrames,
        std::vector<std::vector<uint8_t>>& outFrames,
        float& flowMs,
        float& genMs,
        float& quality);

    // 光流计时查询（供 UI 展示）
    float lastFlowMs() const { return lastFlowMs_; }
    float lastGenMs() const { return lastGenMs_; }
    float lastTotalMs() const { return lastTotalMs_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;

    float lastFlowMs_ = 0.0f;
    float lastGenMs_ = 0.0f;
    float lastTotalMs_ = 0.0f;
};

} // namespace Lingjing
