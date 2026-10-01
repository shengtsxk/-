// ============================================================================
// kernels/cuda/frame_blend.cu
// 灵境 Lingjing — CUDA 遮挡感知帧合成
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

namespace Lingjing {
namespace Cuda {

// ============================================================================
// 遮挡感知合成
// ============================================================================

__global__ void occlusionAwareBlendKernel(
    const float* __restrict__ warpFrom1,
    const float* __restrict__ warpFrom0,
    const float* __restrict__ occProb,
    float* __restrict__ output,
    int W, int H, int pitch,
    float alpha)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float p = occProb[idx];

    float baseW_fwd = 1.0f - alpha;
    float bias = (1.0f - 2.0f * alpha) * p;
    float w_fwd = baseW_fwd + bias;

    if (w_fwd < 0.0f) w_fwd = 0.0f;
    if (w_fwd > 1.0f) w_fwd = 1.0f;

    float w_bwd = 1.0f - w_fwd;

    float a = warpFrom1[idx];
    float b = warpFrom0[idx];

    output[idx] = w_fwd * b + w_bwd * a;
}

// ============================================================================
// 五级可信度合成
// ============================================================================

__global__ void fiveLevelBlendKernel(
    const float* __restrict__ warpFrom1,
    const float* __restrict__ warpFrom0,
    const float* __restrict__ confidence,
    const unsigned char* __restrict__ level,
    const float* __restrict__ I0,
    const float* __restrict__ I1,
    float* __restrict__ output,
    int W, int H, int pitch,
    float alpha)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    unsigned char lvl = level[idx];

    float result = 0.0f;

    switch (lvl) {
        case 0: {
            // 完全可信：标准混合
            float wf = 1.0f - alpha;
            float wb = alpha;
            result = wf * warpFrom0[idx] + wb * warpFrom1[idx];
            break;
        }
        case 1: {
            // 前向可信
            result = warpFrom0[idx];
            break;
        }
        case 2: {
            // 后向可信
            result = warpFrom1[idx];
            break;
        }
        case 3: {
            // 遮挡区域：连续权重
            float cf = confidence[idx];
            float cb = 1.0f - cf;
            float total = cf + cb + 1e-6f;
            float wf = cf / total;
            float wb = cb / total;
            result = wf * warpFrom0[idx] + wb * warpFrom1[idx];
            break;
        }
        case 4:
        default: {
            // 完全不可信：使用最近帧
            result = (alpha < 0.5f) ? I0[idx] : I1[idx];
            break;
        }
    }

    output[idx] = result;
}

// ============================================================================
// 多帧合成
// ============================================================================

__global__ void multiFrameBlendKernel(
    const float* __restrict__ multiWarpFrom1,
    const float* __restrict__ multiWarpFrom0,
    const float* __restrict__ occProb,
    float* __restrict__ multiOutput,
    int W, int H, int pitch,
    int numFrames,
    float alphaStart,
    float alphaStep)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float p = occProb[idx];
    int planeSize = W * H;

    for (int n = 0; n < numFrames; ++n) {
        float alpha = alphaStart + static_cast<float>(n) * alphaStep;

        float baseW_fwd = 1.0f - alpha;
        float bias = (1.0f - 2.0f * alpha) * p;
        float w_fwd = baseW_fwd + bias;

        if (w_fwd < 0.0f) w_fwd = 0.0f;
        if (w_fwd > 1.0f) w_fwd = 1.0f;

        float w_bwd = 1.0f - w_fwd;

        float a = multiWarpFrom1[n * planeSize + idx];
        float b = multiWarpFrom0[n * planeSize + idx];

        multiOutput[n * planeSize + idx] = w_fwd * b + w_bwd * a;
    }
}

// ============================================================================
// 时域平滑
// ============================================================================

__global__ void temporalSmoothKernel(
    const float* __restrict__ currentInterp,
    const float* __restrict__ prevInterp,
    const float* __restrict__ occProb,
    float* __restrict__ smoothed,
    int W, int H, int pitch,
    float historyWeight)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float p = occProb[idx];
    float w = historyWeight * (1.0f - p);

    smoothed[idx] = (1.0f - w) * currentInterp[idx] + w * prevInterp[idx];
}

// ============================================================================
// 颜色空间转换
// ============================================================================

__global__ void rgbToLumaKernel(
    const float* __restrict__ r,
    const float* __restrict__ g,
    const float* __restrict__ b,
    float* __restrict__ luma,
    int total)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= total) return;

    luma[i] = 0.299f * r[i] + 0.587f * g[i] + 0.114f * b[i];
}

__global__ void lumaToRgbKernel(
    const float* __restrict__ luma,
    float* __restrict__ r,
    float* __restrict__ g,
    float* __restrict__ b,
    int total)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= total) return;

    r[i] = luma[i];
    g[i] = luma[i];
    b[i] = luma[i];
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchOcclusionAwareBlend(
    const float* warpFrom1,
    const float* warpFrom0,
    const float* occProb,
    float* output,
    int W, int H,
    float alpha,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    occlusionAwareBlendKernel<<<grid, block, 0, stream>>>(
        warpFrom1, warpFrom0, occProb, output,
        W, H, W, alpha);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchFiveLevelBlend(
    const float* warpFrom1,
    const float* warpFrom0,
    const float* confidence,
    const unsigned char* level,
    const float* I0,
    const float* I1,
    float* output,
    int W, int H,
    float alpha,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    fiveLevelBlendKernel<<<grid, block, 0, stream>>>(
        warpFrom1, warpFrom0, confidence, level,
        I0, I1, output, W, H, W, alpha);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchMultiFrameBlend(
    const float* multiWarpFrom1,
    const float* multiWarpFrom0,
    const float* occProb,
    float* multiOutput,
    int W, int H,
    int numFrames,
    float alphaStart,
    float alphaStep,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    multiFrameBlendKernel<<<grid, block, 0, stream>>>(
        multiWarpFrom1, multiWarpFrom0, occProb, multiOutput,
        W, H, W, numFrames, alphaStart, alphaStep);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchTemporalSmooth(
    const float* currentInterp,
    const float* prevInterp,
    const float* occProb,
    float* smoothed,
    int W, int H,
    float historyWeight,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    temporalSmoothKernel<<<grid, block, 0, stream>>>(
        currentInterp, prevInterp, occProb, smoothed,
        W, H, W, historyWeight);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchRgbToLuma(
    const float* r,
    const float* g,
    const float* b,
    float* luma,
    int total,
    cudaStream_t stream)
{
    int blocks = (total + 255) / 256;

    rgbToLumaKernel<<<blocks, 256, 0, stream>>>(r, g, b, luma, total);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchLumaToRgb(
    const float* luma,
    float* r,
    float* g,
    float* b,
    int total,
    cudaStream_t stream)
{
    int blocks = (total + 255) / 256;

    lumaToRgbKernel<<<blocks, 256, 0, stream>>>(luma, r, g, b, total);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing