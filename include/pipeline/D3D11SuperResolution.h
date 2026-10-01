#pragma once

#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace Lingjing {

// ============================================================================
// D3D11 通用超分辨率（ED-ASR：Edge-Directed Adaptive Super-Resolution）
//
// 原创轻量算法：亮度梯度检测边缘方向 → 沿边缘切向窄核保清晰、
// 法向平滑去锯齿 → 高频细节重建 → 非锐化掩模。无 Tensor Core/深度学习
// 依赖，任何支持 D3D11 compute shader 的显卡（NVIDIA/Intel/AMD）均可运行。
//
// 性能目标（1080p→4K，2x）：3060 级独显 ≤5ms，B580 级 ≤2ms（估）。
// 画质目标：显著优于双三次（边缘保持 + 细节增强），非深度学习级质量，
// 42dB PSNR 属深度学习模型指标，本模块不作此承诺。
// ============================================================================

class D3D11SuperResolution {
public:
    D3D11SuperResolution();
    ~D3D11SuperResolution();

    // 创建 D3D11 设备并编译超分内核
    bool initialize();
    void shutdown();
    bool isReady() const { return ready_; }

    // 将 BGRA8 帧放大 scale 倍（支持 2/3/4/5）
    // frame: 输入 BGRA8（w x h）；out: 输出 BGRA8（w*scale x h*scale）
    bool upscale(
        const uint8_t* frame,
        int width,
        int height,
        int scale,
        std::vector<uint8_t>& out,
        float& ms);

    // 上一次超分耗时（供 UI/自检展示）
    float lastMs() const { return lastMs_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;

    float lastMs_ = 0.0f;
};

} // namespace Lingjing
