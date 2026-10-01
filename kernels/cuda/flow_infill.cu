// ============================================================================
// kernels/cuda/flow_infill.cu
// 灵境 Lingjing — CUDA 遮挡区域光流修补
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

namespace Lingjing {
namespace Cuda {

// ============================================================================
// 距离倒数加权修补
// ============================================================================

__global__ void occlusionInfillKernel(
    const float2* __restrict__ flowIn,
    const float* __restrict__ occProb,
    float2* __restrict__ flowOut,
    int W, int H, int pitch,
    int radius,
    float occThresh)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    if (occProb[idx] < occThresh) {
        flowOut[idx] = flowIn[idx];
        return;
    }

    float sumU = 0.0f, sumV = 0.0f, sumW = 0.0f;

    int x0 = max(0, x - radius);
    int x1 = min(W - 1, x + radius);
    int y0 = max(0, y - radius);
    int y1 = min(H - 1, y + radius);

    for (int ny = y0; ny <= y1; ++ny) {
        for (int nx = x0; nx <= x1; ++nx) {
            int nidx = ny * pitch + nx;
            float op = occProb[nidx];
            if (op >= occThresh) continue;

            float dx = static_cast<float>(nx - x);
            float dy = static_cast<float>(ny - y);
            float d2 = dx * dx + dy * dy + 0.5f;
            float w = (1.0f - op) / d2;

            float2 v = flowIn[nidx];
            sumU += w * v.x;
            sumV += w * v.y;
            sumW += w;
        }
    }

    if (sumW > 1e-6f) {
        flowOut[idx].x = sumU / sumW;
        flowOut[idx].y = sumV / sumW;
    } else {
        flowOut[idx] = flowIn[idx];
    }
}

// ============================================================================
// 双向扩散修补（更平滑）
// ============================================================================

__global__ void bilateralInfillKernel(
    const float2* __restrict__ flowIn,
    const float* __restrict__ guide,
    const float* __restrict__ occProb,
    float2* __restrict__ flowOut,
    int W, int H, int pitch,
    int radius,
    float occThresh,
    float sigmaR)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    if (occProb[idx] < occThresh) {
        flowOut[idx] = flowIn[idx];
        return;
    }

    float g0 = guide[idx];
    float sumU = 0.0f, sumV = 0.0f, sumW = 0.0f;
    float invR2 = 1.0f / (2.0f * sigmaR * sigmaR);

    int x0 = max(0, x - radius);
    int x1 = min(W - 1, x + radius);
    int y0 = max(0, y - radius);
    int y1 = min(H - 1, y + radius);

    for (int ny = y0; ny <= y1; ++ny) {
        for (int nx = x0; nx <= x1; ++nx) {
            int nidx = ny * pitch + nx;
            if (occProb[nidx] >= occThresh) continue;

            float dx = static_cast<float>(nx - x);
            float dy = static_cast<float>(ny - y);
            float spatialW = expf(-(dx * dx + dy * dy) * invR2 * 0.5f);

            float dg = guide[nidx] - g0;
            float rangeW = expf(-dg * dg * invR2);

            float w = spatialW * rangeW * (1.0f - occProb[nidx]);

            float2 v = flowIn[nidx];
            sumU += w * v.x;
            sumV += w * v.y;
            sumW += w;
        }
    }

    if (sumW > 1e-6f) {
        flowOut[idx].x = sumU / sumW;
        flowOut[idx].y = sumV / sumW;
    } else {
        flowOut[idx] = flowIn[idx];
    }
}

// ============================================================================
// 多尺度 push-pull 修补
// ============================================================================

__global__ void pushDownKernel(
    const float4* __restrict__ src,     // RGBA: flowU, flowV, weight, 0
    float4* __restrict__ dst,
    int W, int H, int pitch,
    int dstW, int dstH, int dstPitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dstW || y >= dstH) return;

    int sx0 = x * 2;
    int sy0 = y * 2;

    float4 sum = make_float4(0, 0, 0, 0);
    int count = 0;

    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int sx = sx0 + dx;
            int sy = sy0 + dy;
            if (sx >= W || sy >= H) continue;

            int sidx = sy * pitch + sx;
            float4 v = src[sidx];
            float w = v.z;

            sum.x += v.x * w;
            sum.y += v.y * w;
            sum.z += w;
            ++count;
        }
    }

    if (sum.z > 1e-6f) {
        dst[y * dstPitch + x] = make_float4(
            sum.x / sum.z,
            sum.y / sum.z,
            sum.z / static_cast<float>(count),
            0.0f);
    } else {
        dst[y * dstPitch + x] = make_float4(0, 0, 0, 0);
    }
}

__global__ void pullUpKernel(
    const float4* __restrict__ coarse,
    const float4* __restrict__ fine,
    float4* __restrict__ output,
    int cW, int cH, int cPitch,
    int fW, int fH, int fPitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= fW || y >= fH) return;

    int idx = y * fPitch + x;
    float4 f = fine[idx];

    // 如果 fine 有有效值，直接用
    if (f.z > 0.9f) {
        output[idx] = f;
        return;
    }

    // 从 coarse 双线性上采样
    float lx = 0.5f * (static_cast<float>(x) + 0.5f) - 0.5f;
    float ly = 0.5f * (static_cast<float>(y) + 0.5f) - 0.5f;

    int x0 = max(0, min(static_cast<int>(lx), cW - 1));
    int y0 = max(0, min(static_cast<int>(ly), cH - 1));
    int x1 = min(x0 + 1, cW - 1);
    int y1 = min(y0 + 1, cH - 1);

    float fx = lx - static_cast<float>(x0);
    float fy = ly - static_cast<float>(y0);

    float4 c00 = coarse[y0 * cPitch + x0];
    float4 c10 = coarse[y0 * cPitch + x1];
    float4 c01 = coarse[y1 * cPitch + x0];
    float4 c11 = coarse[y1 * cPitch + x1];

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    float4 c;
    c.x = w00 * c00.x + w10 * c10.x + w01 * c01.x + w11 * c11.x;
    c.y = w00 * c00.y + w10 * c10.y + w01 * c01.y + w11 * c11.y;
    c.z = w00 * c00.z + w10 * c10.z + w01 * c01.z + w11 * c11.z;
    c.w = 0.0f;

    // 混合
    float blend = f.z;
    float4 result;
    result.x = blend * f.x + (1.0f - blend) * c.x;
    result.y = blend * f.y + (1.0f - blend) * c.y;
    result.z = fmaxf(f.z, c.z);
    result.w = 0.0f;

    output[idx] = result;
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchOcclusionInfill(
    const float2* flowIn,
    const float* occProb,
    float2* flowOut,
    int W, int H,
    int radius,
    float occThresh,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    occlusionInfillKernel<<<grid, block, 0, stream>>>(
        flowIn, occProb, flowOut, W, H, W, radius, occThresh);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchBilateralInfill(
    const float2* flowIn,
    const float* guide,
    const float* occProb,
    float2* flowOut,
    int W, int H,
    int radius,
    float occThresh,
    float sigmaR,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    bilateralInfillKernel<<<grid, block, 0, stream>>>(
        flowIn, guide, occProb, flowOut, W, H, W,
        radius, occThresh, sigmaR);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing