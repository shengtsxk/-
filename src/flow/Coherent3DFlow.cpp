#include "flow/Coherent3DFlow.h"
#include "core/Logger.h"

#ifdef LJ_NVIDIA
#include <cuda_runtime.h>
#endif

#ifdef LJ_INTEL
#include <sycl/sycl.hpp>
#endif

#include <cmath>

namespace Lingjing {

    // ============================================================================
    // CUDA 内核
    // ============================================================================

#ifdef LJ_NVIDIA

    __global__ void coherent3DFlowKernel(
        const float* __restrict__ depth,
        float* __restrict__ flow,
        const float* __restrict__ pose,
        float fx, float fy, float cx, float cy,
        float znear, float zfar,
        int W, int H, int pitch)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= W || y >= H) return;

        int idx = y * pitch + x;

        // 反投影
        float d_rel = depth[idx];

        // 相对深度 -> 度量深度（对数分布）
        float d_metric = znear * powf(zfar / znear, d_rel);

        float xn = (static_cast<float>(x) - cx) / fx;
        float yn = (static_cast<float>(y) - cy) / fy;

        float X = xn * d_metric;
        float Y = yn * d_metric;
        float Z = d_metric;

        // Rodrigues 公式
        float wx = pose[0], wy = pose[1], wz = pose[2];
        float tx = pose[3], ty = pose[4], tz = pose[5];

        float theta2 = wx * wx + wy * wy + wz * wz;
        float theta = sqrtf(theta2 + 1e-12f);
        float sinT = sinf(theta);
        float cosT = cosf(theta);

        float A, B;
        if (theta > 1e-6f) {
            A = sinT / theta;
            B = (1.0f - cosT) / theta2;
        }
        else {
            A = 1.0f;
            B = 0.5f;
        }

        float R00 = 1.0f - B * (wy * wy + wz * wz);
        float R01 = -A * wz + B * wx * wy;
        float R02 = A * wy + B * wx * wz;
        float R10 = A * wz + B * wx * wy;
        float R11 = 1.0f - B * (wx * wx + wz * wz);
        float R12 = -A * wx + B * wy * wz;
        float R20 = -A * wy + B * wx * wz;
        float R21 = A * wx + B * wy * wz;
        float R22 = 1.0f - B * (wx * wx + wy * wy);

        float X2 = R00 * X + R01 * Y + R02 * Z + tx;
        float Y2 = R10 * X + R11 * Y + R12 * Z + ty;
        float Z2 = R20 * X + R21 * Y + R22 * Z + tz;

        float invZ = 1.0f / (Z2 + 1e-6f);

        float u2 = X2 * invZ * fx + cx;
        float v2 = Y2 * invZ * fy + cy;

        flow[idx * 2 + 0] = u2 - static_cast<float>(x);
        flow[idx * 2 + 1] = v2 - static_cast<float>(y);
    }

    __global__ void localRigidBodyFitKernel(
        const float* __restrict__ flow,
        float* __restrict__ flowRefined,
        unsigned char* __restrict__ rigidMask,
        int W, int H, int pitch,
        int radius,
        float residualThreshold)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= W || y >= H) return;

        int idx = y * pitch + x;

        float f0 = flow[idx * 2 + 0];
        float f1 = flow[idx * 2 + 1];

        int n = 0;
        float sumFx = 0.0f, sumFy = 0.0f;

        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;

                int nidx = ny * pitch + nx;
                sumFx += flow[nidx * 2 + 0];
                sumFy += flow[nidx * 2 + 1];
                ++n;
            }
        }

        if (n < 4) {
            flowRefined[idx * 2 + 0] = f0;
            flowRefined[idx * 2 + 1] = f1;
            rigidMask[idx] = 0;
            return;
        }

        float meanFx = sumFx / static_cast<float>(n);
        float meanFy = sumFy / static_cast<float>(n);

        float dfx = f0 - meanFx;
        float dfy = f1 - meanFy;
        float residual = sqrtf(dfx * dfx + dfy * dfy);

        if (residual < residualThreshold) {
            flowRefined[idx * 2 + 0] = meanFx;
            flowRefined[idx * 2 + 1] = meanFy;
            rigidMask[idx] = 1;
        }
        else {
            flowRefined[idx * 2 + 0] = f0;
            flowRefined[idx * 2 + 1] = f1;
            rigidMask[idx] = 0;
        }
    }

    __global__ void reprojectionConsistencyKernel(
        const float* __restrict__ flow,
        float* __restrict__ consistency,
        int W, int H, int pitch)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= W || y >= H) return;

        int idx = y * pitch + x;

        float u = static_cast<float>(x) + flow[idx * 2 + 0];
        float v = static_cast<float>(y) + flow[idx * 2 + 1];

        if (u < 0.0f || v < 0.0f ||
            u > static_cast<float>(W - 1) ||
            v > static_cast<float>(H - 1))
        {
            consistency[idx] = 0.0f;
            return;
        }

        int x0 = static_cast<int>(u);
        int y0 = static_cast<int>(v);
        int x1 = min(x0 + 1, W - 1);
        int y1 = min(y0 + 1, H - 1);
        float fx = u - static_cast<float>(x0);
        float fy = v - static_cast<float>(y0);

        float f00x = flow[(y0 * pitch + x0) * 2 + 0];
        float f00y = flow[(y0 * pitch + x0) * 2 + 1];
        float f10x = flow[(y0 * pitch + x1) * 2 + 0];
        float f10y = flow[(y0 * pitch + x1) * 2 + 1];
        float f01x = flow[(y1 * pitch + x0) * 2 + 0];
        float f01y = flow[(y1 * pitch + x0) * 2 + 1];
        float f11x = flow[(y1 * pitch + x1) * 2 + 0];
        float f11y = flow[(y1 * pitch + x1) * 2 + 1];

        float w00 = (1.0f - fx) * (1.0f - fy);
        float w10 = fx * (1.0f - fy);
        float w01 = (1.0f - fx) * fy;
        float w11 = fx * fy;

        float bx = w00 * f00x + w10 * f10x + w01 * f01x + w11 * f11x;
        float by = w00 * f00y + w10 * f10y + w01 * f01y + w11 * f11y;

        float ex = flow[idx * 2 + 0] + bx;
        float ey = flow[idx * 2 + 1] + by;

        float err = sqrtf(ex * ex + ey * ey);
        consistency[idx] = expf(-err * err / 0.5f);
    }

#endif

    // ============================================================================
    // Impl
    // ============================================================================

    struct Coherent3DFlowEngine::Impl {
        GpuTexture cachedFlow;
        GpuTexture cachedConsistency;
        GpuTexture cachedRigidMask;
        GpuTexture cachedRefinedFlow;
        uint32_t cachedWidth = 0;
        uint32_t cachedHeight = 0;
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    Coherent3DFlowEngine::Coherent3DFlowEngine()
        : impl_(std::make_unique<Impl>()) {
    }

    Coherent3DFlowEngine::~Coherent3DFlowEngine() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool Coherent3DFlowEngine::initialize(IGpuContext* gpuContext) {
        if (initialized_) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("Coherent3DFlow: invalid GPU context");
            return false;
        }

        gpuContext_ = gpuContext;
        initialized_ = true;

        LOG_INFO("Coherent3DFlowEngine initialized");
        return true;
    }

    void Coherent3DFlowEngine::shutdown() {
        if (!initialized_) return;

        Coherent3DFlowResult _tmp{}; freeResult(_tmp);

        gpuContext_ = nullptr;
        initialized_ = false;
    }

    // ============================================================================
    // 计算
    // ============================================================================

    bool Coherent3DFlowEngine::compute(
        const GpuTexture& depth,
        const std::array<float, 6>& pose,
        const CameraIntrinsics& intrinsics,
        Coherent3DFlowResult& result)
    {
        if (!initialized_ || !depth.valid()) {
            LOG_ERROR("Coherent3DFlow: not ready or invalid depth");
            return false;
        }

        uint32_t W = depth.width;
        uint32_t H = depth.height;

        // 分配或复用缓冲
        if (impl_->cachedWidth != W || impl_->cachedHeight != H) {
            Coherent3DFlowResult _tmp{}; freeResult(_tmp);

            // 分配新缓冲
            auto* ctx = gpuContext_;

            auto flowBuf = ctx->createTexture(W, H, TextureFormat::R32G32_FLOAT);
            auto consistBuf = ctx->createTexture(W, H, TextureFormat::R32_FLOAT);
            auto maskBuf = ctx->createTexture(W, H, TextureFormat::R8_UNORM);
            auto refinedBuf = ctx->createTexture(W, H, TextureFormat::R32G32_FLOAT);

            if (!flowBuf || !consistBuf || !maskBuf || !refinedBuf) {
                LOG_ERROR("Coherent3DFlow: buffer allocation failed");
                return false;
            }

            impl_->cachedFlow.nativeHandle = flowBuf->nativeHandle();
            impl_->cachedFlow.width = W;
            impl_->cachedFlow.height = H;
            impl_->cachedFlow.format = TextureFormat::R32G32_FLOAT;

            impl_->cachedConsistency.nativeHandle = consistBuf->nativeHandle();
            impl_->cachedConsistency.width = W;
            impl_->cachedConsistency.height = H;
            impl_->cachedConsistency.format = TextureFormat::R32_FLOAT;

            impl_->cachedRigidMask.nativeHandle = maskBuf->nativeHandle();
            impl_->cachedRigidMask.width = W;
            impl_->cachedRigidMask.height = H;
            impl_->cachedRigidMask.format = TextureFormat::R8_UNORM;

            impl_->cachedRefinedFlow.nativeHandle = refinedBuf->nativeHandle();
            impl_->cachedRefinedFlow.width = W;
            impl_->cachedRefinedFlow.height = H;
            impl_->cachedRefinedFlow.format = TextureFormat::R32G32_FLOAT;

            impl_->cachedWidth = W;
            impl_->cachedHeight = H;
        }

#ifdef LJ_NVIDIA
        // 上传位姿到设备
        float* d_pose = nullptr;
        cudaMalloc(&d_pose, 6 * sizeof(float));
        cudaMemcpy(d_pose, pose.data(), 6 * sizeof(float),
            cudaMemcpyHostToDevice);

        dim3 block(16, 16);
        dim3 grid((W + 15) / 16, (H + 15) / 16);

        // 1. 3D 相干流场
        coherent3DFlowKernel << <grid, block >> > (
            static_cast<const float*>(depth.nativeHandle),
            static_cast<float*>(impl_->cachedFlow.nativeHandle),
            d_pose,
            intrinsics.fx, intrinsics.fy,
            intrinsics.cx, intrinsics.cy,
            intrinsics.znear, intrinsics.zfar,
            static_cast<int>(W), static_cast<int>(H), static_cast<int>(W));

        // 2. 局部刚体拟合
        localRigidBodyFitKernel << <grid, block >> > (
            static_cast<const float*>(impl_->cachedFlow.nativeHandle),
            static_cast<float*>(impl_->cachedRefinedFlow.nativeHandle),
            static_cast<unsigned char*>(impl_->cachedRigidMask.nativeHandle),
            static_cast<int>(W), static_cast<int>(H), static_cast<int>(W),
            3, 0.5f);

        // 3. 重投影一致性
        reprojectionConsistencyKernel << <grid, block >> > (
            static_cast<const float*>(impl_->cachedRefinedFlow.nativeHandle),
            static_cast<float*>(impl_->cachedConsistency.nativeHandle),
            static_cast<int>(W), static_cast<int>(H), static_cast<int>(W));

        cudaFree(d_pose);

        cudaError_t err = cudaGetLastError();
        if (err != cudaSuccess) {
            LOG_ERROR("Coherent3DFlow kernel failed: %s",
                cudaGetErrorString(err));
            return false;
        }
#endif

        // 输出
        result.flow = impl_->cachedRefinedFlow;
        result.consistency = impl_->cachedConsistency;
        result.rigidMask = impl_->cachedRigidMask;
        result.width = W;
        result.height = H;

        return true;
    }

    void Coherent3DFlowEngine::freeResult(Coherent3DFlowResult& result) {
        // 缓冲在 Impl 中管理，这里清空句柄
        result.flow.reset();
        result.consistency.reset();
        result.rigidMask.reset();
        result.width = 0;
        result.height = 0;

        impl_->cachedWidth = 0;
        impl_->cachedHeight = 0;
    }

} // namespace Lingjing