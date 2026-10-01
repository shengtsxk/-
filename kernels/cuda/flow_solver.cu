// ============================================================================
// kernels/cuda/flow_solver.cu
// 灵境 Lingjing — CUDA 变分光流求解器
//
// 实现双向对称变分光流的完整求解：
//   1. 图像金字塔构建
//   2. 红黑 Gauss-Seidel 迭代
//   3. Charbonnier 鲁棒惩罚
//   4. 对称耦合项
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math_constants.h>

// ============================================================================
// 常量
// ============================================================================

#define LJ_EPS_CBN       1e-3f
#define LJ_LAMBDA        0.30f
#define LJ_OMEGA         1.0f
#define LJ_GAMMA         1.5f

namespace Lingjing {
namespace Cuda {

// ============================================================================
// 辅助设备函数
// ============================================================================

__device__ __forceinline__ float bilinearSample(
    const float* __restrict__ img,
    int W, int H, int pitch,
    float x, float y)
{
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x > static_cast<float>(W - 1)) x = static_cast<float>(W - 1);
    if (y > static_cast<float>(H - 1)) y = static_cast<float>(H - 1);

    int x0 = static_cast<int>(x);
    int y0 = static_cast<int>(y);
    int x1 = (x0 + 1 < W) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < H) ? y0 + 1 : y0;

    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);

    float a = img[y0 * pitch + x0];
    float b = img[y0 * pitch + x1];
    float c = img[y1 * pitch + x0];
    float d = img[y1 * pitch + x1];

    return (1.0f - fx) * (1.0f - fy) * a
         + fx * (1.0f - fy) * b
         + (1.0f - fx) * fy * c
         + fx * fy * d;
}

__device__ __forceinline__ float2 bilinearSampleFlow(
    const float2* __restrict__ flow,
    int W, int H, int pitch,
    float x, float y)
{
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x > static_cast<float>(W - 1)) x = static_cast<float>(W - 1);
    if (y > static_cast<float>(H - 1)) y = static_cast<float>(H - 1);

    int x0 = static_cast<int>(x);
    int y0 = static_cast<int>(y);
    int x1 = (x0 + 1 < W) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < H) ? y0 + 1 : y0;

    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);

    float2 a = flow[y0 * pitch + x0];
    float2 b = flow[y0 * pitch + x1];
    float2 c = flow[y1 * pitch + x0];
    float2 d = flow[y1 * pitch + x1];

    float2 r;
    r.x = (1.0f - fx) * (1.0f - fy) * a.x
        + fx * (1.0f - fy) * b.x
        + (1.0f - fx) * fy * c.x
        + fx * fy * d.x;
    r.y = (1.0f - fx) * (1.0f - fy) * a.y
        + fx * (1.0f - fy) * b.y
        + (1.0f - fx) * fy * c.y
        + fx * fy * d.y;
    return r;
}

// ============================================================================
// 红黑 SOR 迭代内核
//
// 核心思想：将像素按 (x+y)&1 分为红黑两组，
// 同组内任意两像素不相邻，可完全并行更新。
// 相比 Jacobi 双缓冲，红黑法可用单缓冲原地更新。
// ============================================================================

__global__ void rbSORKernel(
    float2* __restrict__ fwdFlow,
    float2* __restrict__ bwdFlow,
    const float* __restrict__ I0,
    const float* __restrict__ I1,
    int W, int H, int pitch,
    float lambda, float omega, float gamma,
    int parity)
{
    int lane = threadIdx.x & 31;
    int warpId = (blockIdx.x * blockDim.x + threadIdx.x) >> 5;

    int warpsPerRow = (W + 31) >> 5;
    int row = warpId / warpsPerRow;
    int colGroup = warpId % warpsPerRow;

    int xBase = colGroup << 5;
    int x = xBase + lane;

    if (row >= H || x >= W) return;

    bool active = (((x + row) & 1) == parity);

    float2 wf = fwdFlow[row * pitch + x];
    float2 wb = bwdFlow[row * pitch + x];

    // warp shuffle 邻居
    float2 wfL, wfR, wbL, wbR;
    wfL.x = __shfl_up_sync(0xffffffff, wf.x, 1);
    wfL.y = __shfl_up_sync(0xffffffff, wf.y, 1);
    wfR.x = __shfl_down_sync(0xffffffff, wf.x, 1);
    wfR.y = __shfl_down_sync(0xffffffff, wf.y, 1);
    wbL.x = __shfl_up_sync(0xffffffff, wb.x, 1);
    wbL.y = __shfl_up_sync(0xffffffff, wb.y, 1);
    wbR.x = __shfl_down_sync(0xffffffff, wb.x, 1);
    wbR.y = __shfl_down_sync(0xffffffff, wb.y, 1);

    if (lane == 0) {
        int xL = (x - 1 >= 0) ? x - 1 : 0;
        wfL = fwdFlow[row * pitch + xL];
        wbL = bwdFlow[row * pitch + xL];
    }
    if (lane == 31) {
        int xR = (x + 1 < W) ? x + 1 : (W - 1);
        wfR = fwdFlow[row * pitch + xR];
        wbR = bwdFlow[row * pitch + xR];
    }

    int yU = (row - 1 >= 0) ? row - 1 : 0;
    int yD = (row + 1 < H) ? row + 1 : (H - 1);

    float2 fU = fwdFlow[yU * pitch + x];
    float2 fD = fwdFlow[yD * pitch + x];
    float2 bU = bwdFlow[yU * pitch + x];
    float2 bD = bwdFlow[yD * pitch + x];

    if (!active) return;

    int idx = row * pitch + x;

    // ==================== 前向流 ====================
    float uBarF = 0.25f * (wfL.x + wfR.x + fU.x + fD.x);
    float vBarF = 0.25f * (wfL.y + wfR.y + fU.y + fD.y);

    float wfx = static_cast<float>(x) + wf.x;
    float wfy = static_cast<float>(row) + wf.y;

    float I1w = bilinearSample(I1, W, H, pitch, wfx, wfy);
    float IzF = I1w - I0[idx];

    float IxF = 0.5f * (
        bilinearSample(I1, W, H, pitch, wfx + 1.0f, wfy) -
        bilinearSample(I1, W, H, pitch, wfx - 1.0f, wfy));
    float IyF = 0.5f * (
        bilinearSample(I1, W, H, pitch, wfx, wfy + 1.0f) -
        bilinearSample(I1, W, H, pitch, wfx, wfy - 1.0f));

    float psiDF = IzF / sqrtf(IzF * IzF + LJ_EPS_CBN * LJ_EPS_CBN);

    float dfLx = wf.x - wfL.x, dfLy = wf.y - wfL.y;
    float dfRx = wfR.x - wf.x, dfRy = wfR.y - wf.y;
    float dfUx = wf.x - fU.x, dfUy = wf.y - fU.y;
    float dfDx = fD.x - wf.x, dfDy = fD.y - wf.y;

    float gradF = 0.25f * (dfLx * dfLx + dfLy * dfLy
                          + dfRx * dfRx + dfRy * dfRy
                          + dfUx * dfUx + dfUy * dfUy
                          + dfDx * dfDx + dfDy * dfDy)
                + LJ_EPS_CBN * LJ_EPS_CBN;

    float psiSF = 1.0f / (2.0f * sqrtf(gradF));

    float2 bwdAt = bilinearSampleFlow(bwdFlow, W, H, pitch, wfx, wfy);
    float symErrFx = wf.x + bwdAt.x;
    float symErrFy = wf.y + bwdAt.y;

    float denomF = 4.0f * lambda * psiSF + 1e-6f;

    float uNewF = uBarF - (psiDF * IxF + 2.0f * gamma * symErrFx) / denomF;
    float vNewF = vBarF - (psiDF * IyF + 2.0f * gamma * symErrFy) / denomF;

    fwdFlow[idx].x = (1.0f - omega) * wf.x + omega * uNewF;
    fwdFlow[idx].y = (1.0f - omega) * wf.y + omega * vNewF;

    // ==================== 反向流 ====================
    float uBarB = 0.25f * (wbL.x + wbR.x + bU.x + bD.x);
    float vBarB = 0.25f * (wbL.y + wbR.y + bU.y + bD.y);

    float wbx = static_cast<float>(x) + wb.x;
    float wby = static_cast<float>(row) + wb.y;

    float I0w = bilinearSample(I0, W, H, pitch, wbx, wby);
    float IzB = I0w - I1[idx];

    float IxB = 0.5f * (
        bilinearSample(I0, W, H, pitch, wbx + 1.0f, wby) -
        bilinearSample(I0, W, H, pitch, wbx - 1.0f, wby));
    float IyB = 0.5f * (
        bilinearSample(I0, W, H, pitch, wbx, wby + 1.0f) -
        bilinearSample(I0, W, H, pitch, wbx, wby - 1.0f));

    float psiDB = IzB / sqrtf(IzB * IzB + LJ_EPS_CBN * LJ_EPS_CBN);

    float dbLx = wb.x - wbL.x, dbLy = wb.y - wbL.y;
    float dbRx = wbR.x - wb.x, dbRy = wbR.y - wb.y;
    float dbUx = wb.x - bU.x, dbUy = wb.y - bU.y;
    float dbDx = bD.x - wb.x, dbDy = bD.y - wb.y;

    float gradB = 0.25f * (dbLx * dbLx + dbLy * dbLy
                          + dbRx * dbRx + dbRy * dbRy
                          + dbUx * dbUx + dbUy * dbUy
                          + dbDx * dbDx + dbDy * dbDy)
                + LJ_EPS_CBN * LJ_EPS_CBN;

    float psiSB = 1.0f / (2.0f * sqrtf(gradB));

    float2 fwdAt = bilinearSampleFlow(fwdFlow, W, H, pitch, wbx, wby);
    float symErrBx = wb.x + fwdAt.x;
    float symErrBy = wb.y + fwdAt.y;

    float denomB = 4.0f * lambda * psiSB + 1e-6f;

    float uNewB = uBarB - (psiDB * IxB + 2.0f * gamma * symErrBx) / denomB;
    float vNewB = vBarB - (psiDB * IyB + 2.0f * gamma * symErrBy) / denomB;

    bwdFlow[idx].x = (1.0f - omega) * wb.x + omega * uNewB;
    bwdFlow[idx].y = (1.0f - omega) * wb.y + omega * vNewB;
}

// ============================================================================
// 金字塔下采样 — 水平趟（5-tap binomial）
// ============================================================================

__global__ void pyramidDownHKernel(
    const float* __restrict__ src,
    float* __restrict__ dst,
    int srcW, int srcH, int srcPitch,
    int dstW, int dstH, int dstPitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dstW || y >= dstH) return;

    int cx = x << 1;
    const float* row = src + (y << 1) * srcPitch;

    float a = (cx - 2 >= 0)      ? row[cx - 2] : row[0];
    float b = (cx - 1 >= 0)      ? row[cx - 1] : row[0];
    float c = row[cx];
    float d = (cx + 1 < srcW)    ? row[cx + 1] : row[srcW - 1];
    float e = (cx + 2 < srcW)    ? row[cx + 2] : row[srcW - 1];

    dst[y * dstPitch + x] = (a + 4.0f * b + 6.0f * c + 4.0f * d + e)
                             * (1.0f / 16.0f);
}

// ============================================================================
// 金字塔下采样 — 垂直趟
// ============================================================================

__global__ void pyramidDownVKernel(
    const float* __restrict__ src,
    float* __restrict__ dst,
    int srcW, int srcH, int srcPitch,
    int dstW, int dstH, int dstPitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dstW || y >= dstH) return;

    int cy = y << 1;

    auto load = [&](int yy) -> float {
        int c = yy < 0 ? 0 : (yy >= srcH ? srcH - 1 : yy);
        return src[c * srcPitch + x];
    };

    float a = load(cy - 2);
    float b = load(cy - 1);
    float c = load(cy);
    float d = load(cy + 1);
    float e = load(cy + 2);

    dst[y * dstPitch + x] = (a + 4.0f * b + 6.0f * c + 4.0f * d + e)
                             * (1.0f / 16.0f);
}

// ============================================================================
// 光流上采样
// ============================================================================

__global__ void flowUpsampleKernel(
    const float2* __restrict__ coarse,
    float2* __restrict__ fine,
    int cW, int cH, int cPitch,
    int fW, int fH, int fPitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= fW || y >= fH) return;

    float lx = 0.5f * (static_cast<float>(x) + 0.5f) - 0.5f;
    float ly = 0.5f * (static_cast<float>(y) + 0.5f) - 0.5f;

    int x0 = static_cast<int>(floorf(lx));
    int y0 = static_cast<int>(floorf(ly));
    int x1 = min(x0 + 1, cW - 1);
    int y1 = min(y0 + 1, cH - 1);
    x0 = max(0, min(x0, cW - 1));
    y0 = max(0, min(y0, cH - 1));

    float fx = lx - static_cast<float>(x0);
    float fy = ly - static_cast<float>(y0);

    float2 v00 = coarse[y0 * cPitch + x0];
    float2 v10 = coarse[y0 * cPitch + x1];
    float2 v01 = coarse[y1 * cPitch + x0];
    float2 v11 = coarse[y1 * cPitch + x1];

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    float2 r;
    r.x = w00 * v00.x + w10 * v10.x + w01 * v01.x + w11 * v11.x;
    r.y = w00 * v00.y + w10 * v10.y + w01 * v01.y + w11 * v11.y;

    r.x *= static_cast<float>(fW) / static_cast<float>(cW);
    r.y *= static_cast<float>(fH) / static_cast<float>(cH);

    fine[y * fPitch + x] = r;
}

// ============================================================================
// 中值滤波（去除光流脉冲噪声）
// ============================================================================

__global__ void flowMedianKernel(
    const float2* __restrict__ in,
    float2* __restrict__ out,
    int W, int H, int pitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    float bu[9];
    float bv[9];
    int n = 0;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = max(0, min(x + dx, W - 1));
            int ny = max(0, min(y + dy, H - 1));
            float2 v = in[ny * pitch + nx];
            bu[n] = v.x;
            bv[n] = v.y;
            ++n;
        }
    }

    // 插入排序
    for (int i = 1; i < 9; ++i) {
        float ku = bu[i], kv = bv[i];
        int j = i - 1;
        while (j >= 0 && bu[j] > ku) {
            bu[j + 1] = bu[j];
            bv[j + 1] = bv[j];
            --j;
        }
        bu[j + 1] = ku;
        bv[j + 1] = kv;
    }

    float2 r;
    r.x = bu[4];
    r.y = bv[4];
    out[y * pitch + x] = r;
}

// ============================================================================
// NaN/Inf 修复
// ============================================================================

__global__ void sanitizeFlowKernel(
    float2* __restrict__ flow,
    int W, int H, int pitch,
    float maxMag)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float2 v = flow[idx];

    if (!isfinite(v.x) || !isfinite(v.y)) {
        flow[idx] = make_float2(0.0f, 0.0f);
        return;
    }

    float m2 = v.x * v.x + v.y * v.y;
    if (m2 > maxMag * maxMag) {
        float s = maxMag / sqrtf(m2);
        v.x *= s;
        v.y *= s;
    }

    flow[idx] = v;
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchRbSOR(
    float2* fwdFlow,
    float2* bwdFlow,
    const float* I0,
    const float* I1,
    int W, int H,
    float lambda, float omega, float gamma,
    cudaStream_t stream)
{
    int threadsPerBlock = 128;
    int warpsPerRow = (W + 31) >> 5;
    int totalWarps = warpsPerRow * H;
    int blocks = (totalWarps * 32 + threadsPerBlock - 1) / threadsPerBlock;

    rbSORKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        fwdFlow, bwdFlow, I0, I1,
        W, H, W,
        lambda, omega, gamma,
        0);

    rbSORKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        fwdFlow, bwdFlow, I0, I1,
        W, H, W,
        lambda, omega, gamma,
        1);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchPyramidDown(
    const float* src,
    float* dst,
    int srcW, int srcH,
    int dstW, int dstH,
    float* tempBuffer,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 gridH((dstW + 15) / 16, (srcH + 15) / 16);
    dim3 gridV((dstW + 15) / 16, (dstH + 15) / 16);

    pyramidDownHKernel<<<gridH, block, 0, stream>>>(
        src, tempBuffer,
        srcW, srcH, srcW,
        dstW, srcH, dstW);

    pyramidDownVKernel<<<gridV, block, 0, stream>>>(
        tempBuffer, dst,
        dstW, srcH, dstW,
        dstW, dstH, dstW);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchFlowUpsample(
    const float2* coarse,
    float2* fine,
    int cW, int cH,
    int fW, int fH,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((fW + 15) / 16, (fH + 15) / 16);

    flowUpsampleKernel<<<grid, block, 0, stream>>>(
        coarse, fine,
        cW, cH, cW,
        fW, fH, fW);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchFlowMedian(
    const float2* in,
    float2* out,
    int W, int H,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    flowMedianKernel<<<grid, block, 0, stream>>>(
        in, out, W, H, W);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchSanitizeFlow(
    float2* flow,
    int W, int H,
    float maxMag,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    sanitizeFlowKernel<<<grid, block, 0, stream>>>(
        flow, W, H, W, maxMag);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing