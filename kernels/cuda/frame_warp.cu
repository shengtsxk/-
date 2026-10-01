// ============================================================================
// kernels/cuda/frame_warp.cu
// 灵境 Lingjing — CUDA 帧 warp（双三次采样）
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

namespace Lingjing {
namespace Cuda {

// ============================================================================
// Catmull-Rom 三次权重
// ============================================================================

__device__ __forceinline__ float cubicWeight(float x, float a) {
    float ax = fabsf(x);
    if (ax <= 1.0f) {
        return (a + 2.0f) * ax * ax * ax
             - (a + 3.0f) * ax * ax + 1.0f;
    } else if (ax < 2.0f) {
        return a * ax * ax * ax
             - 5.0f * a * ax * ax
             + 8.0f * a * ax - 4.0f * a;
    }
    return 0.0f;
}

__device__ __forceinline__ float bicubicSample(
    const float* __restrict__ img,
    int W, int H, int pitch,
    float x, float y)
{
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x > static_cast<float>(W - 1)) x = static_cast<float>(W - 1);
    if (y > static_cast<float>(H - 1)) y = static_cast<float>(H - 1);

    int xi = static_cast<int>(x);
    int yi = static_cast<int>(y);
    float fx = x - static_cast<float>(xi);
    float fy = y - static_cast<float>(yi);

    const float A = -0.75f;
    float sum = 0.0f;
    float wsum = 0.0f;

    #pragma unroll
    for (int j = -1; j <= 2; ++j) {
        #pragma unroll
        for (int i = -1; i <= 2; ++i) {
            int sx = xi + i;
            int sy = yi + j;
            if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;

            float wx = cubicWeight(fx - static_cast<float>(i), A);
            float wy = cubicWeight(fy - static_cast<float>(j), A);
            float w = wx * wy;

            sum += w * img[sy * pitch + sx];
            wsum += w;
        }
    }

    return (wsum > 1e-6f) ? (sum / wsum) : 0.0f;
}

// ============================================================================
// 双向 warp 内核
// ============================================================================

__global__ void bidirectionalWarpKernel(
    const float* __restrict__ I0,
    const float* __restrict__ I1,
    const float2* __restrict__ fwdFlow,
    const float2* __restrict__ bwdFlow,
    float* __restrict__ warpFrom1,
    float* __restrict__ warpFrom0,
    int W, int H, int pitch,
    float alpha)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    // 从 I1 反向 warp
    float2 bf = bwdFlow[idx];
    float fwx = static_cast<float>(x) + alpha * bf.x;
    float fwy = static_cast<float>(y) + alpha * bf.y;
    warpFrom1[idx] = bicubicSample(I1, W, H, pitch, fwx, fwy);

    // 从 I0 正向 warp
    float2 ff = fwdFlow[idx];
    float bwx = static_cast<float>(x) + (1.0f - alpha) * ff.x;
    float bwy = static_cast<float>(y) + (1.0f - alpha) * ff.y;
    warpFrom0[idx] = bicubicSample(I0, W, H, pitch, bwx, bwy);
}

// ============================================================================
// 多帧 warp（批量生成 N 帧）
// ============================================================================

__global__ void multiFrameWarpKernel(
    const float* __restrict__ I0,
    const float* __restrict__ I1,
    const float2* __restrict__ fwdFlow,
    const float2* __restrict__ bwdFlow,
    float* __restrict__ multiWarpFrom1,
    float* __restrict__ multiWarpFrom0,
    int W, int H, int pitch,
    int numFrames,
    float alphaStart,
    float alphaStep)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    int planeSize = W * H;

    float2 ff = fwdFlow[idx];
    float2 bf = bwdFlow[idx];

    float fxBase = static_cast<float>(x);
    float fyBase = static_cast<float>(y);

    for (int n = 0; n < numFrames; ++n) {
        float alpha = alphaStart + static_cast<float>(n) * alphaStep;

        float fwx = fxBase + alpha * bf.x;
        float fwy = fyBase + alpha * bf.y;
        multiWarpFrom1[n * planeSize + idx] =
            bicubicSample(I1, W, H, pitch, fwx, fwy);

        float bwx = fxBase + (1.0f - alpha) * ff.x;
        float bwy = fyBase + (1.0f - alpha) * ff.y;
        multiWarpFrom0[n * planeSize + idx] =
            bicubicSample(I0, W, H, pitch, bwx, bwy);
    }
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchBidirectionalWarp(
    const float* I0,
    const float* I1,
    const float2* fwdFlow,
    const float2* bwdFlow,
    float* warpFrom1,
    float* warpFrom0,
    int W, int H,
    float alpha,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    bidirectionalWarpKernel<<<grid, block, 0, stream>>>(
        I0, I1, fwdFlow, bwdFlow,
        warpFrom1, warpFrom0, W, H, W, alpha);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchMultiFrameWarp(
    const float* I0,
    const float* I1,
    const float2* fwdFlow,
    const float2* bwdFlow,
    float* multiWarpFrom1,
    float* multiWarpFrom0,
    int W, int H,
    int numFrames,
    float alphaStart,
    float alphaStep,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    multiFrameWarpKernel<<<grid, block, 0, stream>>>(
        I0, I1, fwdFlow, bwdFlow,
        multiWarpFrom1, multiWarpFrom0,
        W, H, W, numFrames, alphaStart, alphaStep);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing