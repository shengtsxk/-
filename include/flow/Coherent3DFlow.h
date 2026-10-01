#pragma once

#include "core/Types.h"
#include "gpu/IGpuContext.h"
#include <memory>
#include <array>

namespace Lingjing {

    // ============================================================================
    // 相机参数
    // ============================================================================

    struct CameraIntrinsics {
        float fx = 1000.0f;
        float fy = 1000.0f;
        float cx = 960.0f;
        float cy = 540.0f;
        float znear = 0.1f;
        float zfar = 1000.0f;

        void toMatrix(float out[9]) const {
            out[0] = fx; out[1] = 0;  out[2] = cx;
            out[3] = 0;  out[4] = fy; out[5] = cy;
            out[6] = 0;  out[7] = 0;  out[8] = 1;
        }
    };

    // ============================================================================
    // 3D 相干流场结果
    // ============================================================================

    struct Coherent3DFlowResult {
        GpuTexture flow;            // 每像素运动矢量 (RG32F)
        GpuTexture consistency;     // 一致性 (R32F)
        GpuTexture rigidMask;       // 刚体掩码 (R8)
        uint32_t width = 0;
        uint32_t height = 0;

        bool isValid() const { return flow.valid(); }
    };

    // ============================================================================
    // 3D 相干流场引擎
    // ============================================================================

    class Coherent3DFlowEngine {
    public:
        Coherent3DFlowEngine();
        ~Coherent3DFlowEngine();

        bool initialize(IGpuContext* gpuContext);
        void shutdown();

        // 计算 3D 相干流场
        bool compute(const GpuTexture& depth,
            const std::array<float, 6>& pose,
            const CameraIntrinsics& intrinsics,
            Coherent3DFlowResult& result);

        // 释放结果
        void freeResult(Coherent3DFlowResult& result);

        bool isReady() const { return initialized_; }

    private:
        IGpuContext* gpuContext_ = nullptr;
        bool initialized_ = false;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Lingjing