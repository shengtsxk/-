// ============================================================================
// kernels/cuda/flow_consistency.cu
// 灵境 Lingjing — CUDA 前向-后向一致性校验与遮挡推断
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math_constants.h>

namespace Lingjing {
namespace Cuda {

// ============================================================================
// 前向-后向一致性
// ============================================================================

__global__ void forwardBackwardConsistencyKernel(
    const float2* __restrict__ forwardFlow,
    const float2* __restrict__ backwardFlow,
    float* __restrict__ errorMap,
    int W, int H, int pitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float2 wf = forwardFlow[idx];

    float sx = static_cast<float>(x) + wf.x;
    float sy = static_cast<float>(y) + wf.y;

    if (sx < 0.0f || sy < 0.0f ||
        sx > static_cast<float>(W - 1) ||
        sy > static_cast<float>(H - 1))
    {
        errorMap[idx] = 1e6f;
        return;
    }

    int x0 = static_cast<int>(sx);
    int y0 = static_cast<int>(sy);
    int x1 = min(x0 + 1, W - 1);
    int y1 = min(y0 + 1, H - 1);
    float fx = sx - static_cast<float>(x0);
    float fy = sy - static_cast<float>(y0);

    float2 b00 = backwardFlow[y0 * pitch + x0];
    float2 b10 = backwardFlow[y0 * pitch + x1];
    float2 b01 = backwardFlow[y1 * pitch + x0];
    float2 b11 = backwardFlow[y1 * pitch + x1];

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    float bx = w00 * b00.x + w10 * b10.x + w01 * b01.x + w11 * b11.x;
    float by = w00 * b00.y + w10 * b10.y + w01 * b01.y + w11 * b11.y;

    float ex = wf.x + bx;
    float ey = wf.y + by;

    errorMap[idx] = sqrtf(ex * ex + ey * ey);
}

// ============================================================================
// 遮挡概率
// ============================================================================

__global__ void occlusionProbabilityKernel(
    const float* __restrict__ errorMap,
    float* __restrict__ occProb,
    int W, int H, int pitch,
    float tau, float alpha)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float e = errorMap[idx];

    float logit = alpha * (e / tau - 1.0f);
    occProb[idx] = 1.0f / (1.0f + expf(-logit));
}

// ============================================================================
// 边界硬化
// ============================================================================

__global__ void boundaryHardeningKernel(
    const float* __restrict__ occProb,
    float* __restrict__ occSharp,
    int W, int H, int pitch,
    float gradThreshold)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    float gx = 0.0f, gy = 0.0f;
    if (x > 0 && x < W - 1) {
        gx = 0.5f * (occProb[idx + 1] - occProb[idx - 1]);
    }
    if (y > 0 && y < H - 1) {
        gy = 0.5f * (occProb[idx + pitch] - occProb[idx - pitch]);
    }

    float gmag = sqrtf(gx * gx + gy * gy);
    float p = occProb[idx];

    if (gmag > gradThreshold) {
        float sharpened = (p > 0.5f) ? 1.0f : 0.0f;
        occSharp[idx] = 0.7f * p + 0.3f * sharpened;
    } else {
        occSharp[idx] = p;
    }
}

// ============================================================================
// 鲁棒一致性（归一化 + 结构加权）
// ============================================================================

__global__ void robustConsistencyKernel(
    const float2* __restrict__ fwdFlow,
    const float2* __restrict__ bwdFlow,
    const float* __restrict__ localStd,
    const float* __restrict__ structuralConf,
    float* __restrict__ robustError,
    int W, int H, int pitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float2 wf = fwdFlow[idx];

    float sx = static_cast<float>(x) + wf.x;
    float sy = static_cast<float>(y) + wf.y;

    if (sx < 0.0f || sy < 0.0f ||
        sx > static_cast<float>(W - 1) ||
        sy > static_cast<float>(H - 1))
    {
        robustError[idx] = 1e6f;
        return;
    }

    int x0 = static_cast<int>(sx);
    int y0 = static_cast<int>(sy);
    int x1 = min(x0 + 1, W - 1);
    int y1 = min(y0 + 1, H - 1);
    float fx = sx - static_cast<float>(x0);
    float fy = sy - static_cast<float>(y0);

    float2 b00 = bwdFlow[y0 * pitch + x0];
    float2 b10 = bwdFlow[y0 * pitch + x1];
    float2 b01 = bwdFlow[y1 * pitch + x0];
    float2 b11 = bwdFlow[y1 * pitch + x1];

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    float bx = w00 * b00.x + w10 * b10.x + w01 * b01.x + w11 * b11.x;
    float by = w00 * b00.y + w10 * b10.y + w01 * b01.y + w11 * b11.y;

    float ex = wf.x + bx;
    float ey = wf.y + by;
    float rawErr = sqrtf(ex * ex + ey * ey);

    float norm = localStd[idx] + 0.1f;
    float rho = structuralConf[idx];
    float structWeight = 0.3f + 0.7f * fabsf(rho);

    robustError[idx] = (rawErr / norm) / structWeight;
}

// ============================================================================
// 局部标准差
// ============================================================================

__global__ void localFlowStdKernel(
    const float2* __restrict__ flow,
    float* __restrict__ localStd,
    int W, int H, int pitch,
    int radius)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    float sumU = 0.0f, sumV = 0.0f;
    float sumU2 = 0.0f, sumV2 = 0.0f;
    int n = 0;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;

            float2 v = flow[ny * pitch + nx];
            sumU += v.x;
            sumV += v.y;
            sumU2 += v.x * v.x;
            sumV2 += v.y * v.y;
            ++n;
        }
    }

    int idx = y * pitch + x;

    if (n < 2) {
        localStd[idx] = 1.0f;
        return;
    }

    float invN = 1.0f / static_cast<float>(n);
    float meanU = sumU * invN;
    float meanV = sumV * invN;
    float varU = sumU2 * invN - meanU * meanU;
    float varV = sumV2 * invN - meanV * meanV;

    localStd[idx] = sqrtf(fmaxf(varU + varV, 1e-6f));
}

// ============================================================================
// 结构置信度（基于结构张量）
// ============================================================================

__global__ void structuralConfidenceKernel(
    const float* __restrict__ Jxx,
    const float* __restrict__ Jxy,
    const float* __restrict__ Jyy,
    float* __restrict__ rho,
    int W, int H, int pitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    float jxx = Jxx[idx];
    float jxy = Jxy[idx];
    float jyy = Jyy[idx];

    float trace = jxx + jyy;
    float diff = jxx - jyy;
    float disc = sqrtf(diff * diff + 4.0f * jxy * jxy);

    float lam1 = 0.5f * (trace + disc);
    float lam2 = 0.5f * (trace - disc);

    float denom = lam1 + lam2 + 1e-4f;
    rho[idx] = (lam1 - lam2) / denom;
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchConsistency(
    const float2* fwdFlow,
    const float2* bwdFlow,
    float* errorMap,
    int W, int H,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    forwardBackwardConsistencyKernel<<<grid, block, 0, stream>>>(
        fwdFlow, bwdFlow, errorMap, W, H, W);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchOcclusionProbability(
    const float* errorMap,
    float* occProb,
    int W, int H,
    float tau, float alpha,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    occlusionProbabilityKernel<<<grid, block, 0, stream>>>(
        errorMap, occProb, W, H, W, tau, alpha);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchBoundaryHardening(
    const float* occProb,
    float* occSharp,
    int W, int H,
    float gradThreshold,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    boundaryHardeningKernel<<<grid, block, 0, stream>>>(
        occProb, occSharp, W, H, W, gradThreshold);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchLocalFlowStd(
    const float2* flow,
    float* localStd,
    int W, int H,
    int radius,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    localFlowStdKernel<<<grid, block, 0, stream>>>(
        flow, localStd, W, H, W, radius);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchStructuralConfidence(
    const float* Jxx,
    const float* Jxy,
    const float* Jyy,
    float* rho,
    int W, int H,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    structuralConfidenceKernel<<<grid, block, 0, stream>>>(
        Jxx, Jxy, Jyy, rho, W, H, W);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchRobustConsistency(
    const float2* fwdFlow,
    const float2* bwdFlow,
    const float* localStd,
    const float* structuralConf,
    float* robustError,
    int W, int H,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    robustConsistencyKernel<<<grid, block, 0, stream>>>(
        fwdFlow, bwdFlow, localStd, structuralConf, robustError, W, H, W);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing