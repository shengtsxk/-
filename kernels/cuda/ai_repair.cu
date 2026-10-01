// ============================================================================
// kernels/cuda/ai_repair.cu
// 灵境 Lingjing — CUDA AI 修复预处理与后处理
//
// AI 推理本身由 TensorRT 完成，这里实现：
//   1. 输入预处理（归一化、打包）
//   2. 输出后处理（反归一化、混合）
//   3. AI 残差补偿（将 AI 输出与几何流融合）
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

namespace Lingjing {
namespace Cuda {

// ============================================================================
// 输入预处理：NCHW 打包
// ============================================================================

__global__ void packNCHWKernel(
    const float* __restrict__ r,
    const float* __restrict__ g,
    const float* __restrict__ b,
    float* __restrict__ nchw,
    int W, int H,
    float meanR, float meanG, float meanB,
    float stdR, float stdG, float stdB)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    int planeSize = W * H;

    nchw[0 * planeSize + idx] = (r[idx] - meanR) / stdR;
    nchw[1 * planeSize + idx] = (g[idx] - meanG) / stdG;
    nchw[2 * planeSize + idx] = (b[idx] - meanB) / stdB;
}

// ============================================================================
// 输出后处理：NCHW 解包
// ============================================================================

__global__ void unpackNCHWKernel(
    const float* __restrict__ nchw,
    float* __restrict__ r,
    float* __restrict__ g,
    float* __restrict__ b,
    int W, int H,
    float meanR, float meanG, float meanB,
    float stdR, float stdG, float stdB)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    int planeSize = W * H;

    r[idx] = nchw[0 * planeSize + idx] * stdR + meanR;
    g[idx] = nchw[1 * planeSize + idx] * stdG + meanG;
    b[idx] = nchw[2 * planeSize + idx] * stdB + meanB;
}

// ============================================================================
// AI 残差融合
// ============================================================================

__global__ void aiResidualBlendKernel(
    const float* __restrict__ geometric,
    const float* __restrict__ aiOutput,
    const float* __restrict__ confidence,
    float* __restrict__ blended,
    int W, int H,
    float aiWeight)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    float c = confidence[idx];
    float w = aiWeight * c;

    if (w > 1.0f) w = 1.0f;
    if (w < 0.0f) w = 0.0f;

    blended[idx] = (1.0f - w) * geometric[idx] + w * aiOutput[idx];
}

// ============================================================================
// 边缘保持上采样（用于 AI 输出）
// ============================================================================

__global__ void edgeAwareUpsampleKernel(
    const float* __restrict__ lowRes,
    const float* __restrict__ guide,
    float* __restrict__ highRes,
    int lW, int lH,
    int hW, int hH,
    float sigmaR)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= hW || y >= hH) return;

    float scaleX = static_cast<float>(lW) / static_cast<float>(hW);
    float scaleY = static_cast<float>(lH) / static_cast<float>(hH);

    float lx = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
    float ly = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;

    int x0 = static_cast<int>(floorf(lx));
    int y0 = static_cast<int>(floorf(ly));
    int x1 = min(x0 + 1, lW - 1);
    int y1 = min(y0 + 1, lH - 1);
    x0 = max(0, min(x0, lW - 1));
    y0 = max(0, min(y0, lH - 1));

    float fx = lx - static_cast<float>(x0);
    float fy = ly - static_cast<float>(y0);

    float v00 = lowRes[y0 * lW + x0];
    float v10 = lowRes[y0 * lW + x1];
    float v01 = lowRes[y1 * lW + x0];
    float v11 = lowRes[y1 * lW + x1];

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    float result = w00 * v00 + w10 * v10 + w01 * v01 + w11 * v11;

    // 边缘保持校正
    float g = guide[y * hW + x];
    float gLow = 0.25f * (v00 + v10 + v01 + v11);
    float edge = g - gLow;

    float correction = edge * expf(-edge * edge / (2.0f * sigmaR * sigmaR));
    highRes[y * hW + x] = result + correction * 0.3f;
}

// ============================================================================
// 噪点抑制（用于低置信度区域）
// ============================================================================

__global__ void denoiseKernel(
    const float* __restrict__ input,
    const float* __restrict__ confidence,
    float* __restrict__ output,
    int W, int H,
    int radius,
    float sigmaS,
    float sigmaR)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    float c = confidence[idx];

    // 高置信度直接通过
    if (c > 0.9f) {
        output[idx] = input[idx];
        return;
    }

    float center = input[idx];
    float sum = 0.0f;
    float wsum = 0.0f;
    float invS2 = 1.0f / (2.0f * sigmaS * sigmaS);
    float invR2 = 1.0f / (2.0f * sigmaR * sigmaR);

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;

            int nidx = ny * W + nx;
            float v = input[nidx];

            float spatialW = expf(-(dx * dx + dy * dy) * invS2 * 0.5f);
            float dr = v - center;
            float rangeW = expf(-dr * dr * invR2);

            float w = spatialW * rangeW;
            sum += w * v;
            wsum += w;
        }
    }

    float denoised = (wsum > 1e-6f) ? (sum / wsum) : center;
    output[idx] = (1.0f - c) * denoised + c * center;
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchPackNCHW(
    const float* r,
    const float* g,
    const float* b,
    float* nchw,
    int W, int H,
    float meanR, float meanG, float meanB,
    float stdR, float stdG, float stdB,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    packNCHWKernel<<<grid, block, 0, stream>>>(
        r, g, b, nchw, W, H,
        meanR, meanG, meanB, stdR, stdG, stdB);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchUnpackNCHW(
    const float* nchw,
    float* r,
    float* g,
    float* b,
    int W, int H,
    float meanR, float meanG, float meanB,
    float stdR, float stdG, float stdB,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    unpackNCHWKernel<<<grid, block, 0, stream>>>(
        nchw, r, g, b, W, H,
        meanR, meanG, meanB, stdR, stdG, stdB);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchAiResidualBlend(
    const float* geometric,
    const float* aiOutput,
    const float* confidence,
    float* blended,
    int W, int H,
    float aiWeight,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    aiResidualBlendKernel<<<grid, block, 0, stream>>>(
        geometric, aiOutput, confidence, blended, W, H, aiWeight);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchEdgeAwareUpsample(
    const float* lowRes,
    const float* guide,
    float* highRes,
    int lW, int lH,
    int hW, int hH,
    float sigmaR,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((hW + 15) / 16, (hH + 15) / 16);

    edgeAwareUpsampleKernel<<<grid, block, 0, stream>>>(
        lowRes, guide, highRes, lW, lH, hW, hH, sigmaR);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchDenoise(
    const float* input,
    const float* confidence,
    float* output,
    int W, int H,
    int radius,
    float sigmaS,
    float sigmaR,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    denoiseKernel<<<grid, block, 0, stream>>>(
        input, confidence, output, W, H, radius, sigmaS, sigmaR);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing