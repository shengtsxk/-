// ============================================================================
// kernels/cuda/dejelly.cu
// 灵境 Lingjing — CUDA 去果冻算法
//
// 三层处理：
//   1. 各向异性二阶正则
//   2. 二次运动模型拟合
//   3. 散度/旋度抑制
// ============================================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

namespace Lingjing {
namespace Cuda {

// ============================================================================
// 各向异性曲率惩罚
// ============================================================================

__global__ void anisotropicCurvatureKernel(
    const float2* __restrict__ flowIn,
    float2* __restrict__ flowOut,
    const float* __restrict__ Jxx,
    const float* __restrict__ Jxy,
    const float* __restrict__ Jyy,
    int W, int H, int pitch,
    float mu, float omega, float kappa)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float2 c = flowIn[idx];

    float jxx = Jxx[idx];
    float jxy = Jxy[idx];
    float jyy = Jyy[idx];

    float trace = jxx + jyy;
    float diff = jxx - jyy;
    float disc = sqrtf(diff * diff + 4.0f * jxy * jxy);

    float lam1 = 0.5f * (trace + disc);
    float lam2 = 0.5f * (trace - disc);

    float mu1 = 1.0f / sqrtf(1.0f + lam1 / (kappa * kappa));
    float mu2 = 1.0f / sqrtf(1.0f + lam2 / (kappa * kappa));

    float theta = 0.5f * atan2f(2.0f * jxy, diff);
    float e1x = cosf(theta), e1y = sinf(theta);
    float e2x = -e1y, e2y = e1x;

    float Dxx = mu1 * e1x * e1x + mu2 * e2x * e2x;
    float Dxy = mu1 * e1x * e1y + mu2 * e2x * e2y;
    float Dyy = mu1 * e1y * e1y + mu2 * e2y * e2y;

    auto load = [&](int xx, int yy) -> float2 {
        xx = max(0, min(xx, W - 1));
        yy = max(0, min(yy, H - 1));
        return flowIn[yy * pitch + xx];
    };

    float2 uL = load(x - 1, y);
    float2 uR = load(x + 1, y);
    float2 uU = load(x, y - 1);
    float2 uD = load(x, y + 1);
    float2 uLL = load(x - 2, y);
    float2 uRR = load(x + 2, y);
    float2 uUU = load(x, y - 2);
    float2 uDD = load(x, y + 2);

    float uxx_u = uLL.x - 2.0f * uL.x + c.x;
    float uxx_b = c.x - 2.0f * uR.x + uRR.x;
    float uxx = 0.5f * (uxx_u + uxx_b);

    float uyy_u = uUU.x - 2.0f * uU.x + c.x;
    float uyy_b = c.x - 2.0f * uD.x + uDD.x;
    float uyy = 0.5f * (uyy_u + uyy_b);

    float vxx_u = uLL.y - 2.0f * uL.y + c.y;
    float vxx_b = c.y - 2.0f * uR.y + uRR.y;
    float vxx = 0.5f * (vxx_u + vxx_b);

    float vyy_u = uUU.y - 2.0f * uU.y + c.y;
    float vyy_b = c.y - 2.0f * uD.y + uDD.y;
    float vyy = 0.5f * (vyy_u + vyy_b);

    float du = mu * (Dxx * uxx + Dyy * uyy);
    float dv = mu * (Dxx * vxx + Dyy * vyy);

    float2 r;
    r.x = (1.0f - omega) * c.x + omega * (c.x - du);
    r.y = (1.0f - omega) * c.y + omega * (c.y - dv);

    flowOut[idx] = r;
}

// ============================================================================
// 二次运动拟合
// ============================================================================

__global__ void quadraticMotionFitKernel(
    const float2* __restrict__ flowIn,
    float2* __restrict__ flowOut,
    const float* __restrict__ luma,
    int W, int H, int pitch,
    int radius,
    float rangeSigma)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    float lumaCenter = luma[idx];
    float invRange2 = 1.0f / (2.0f * rangeSigma * rangeSigma);

    float sw = 0.0f;
    float sx = 0.0f, sy = 0.0f;
    float sxx = 0.0f, syy = 0.0f, sxy = 0.0f;
    float su = 0.0f, sv = 0.0f;
    float sux = 0.0f, suy = 0.0f;
    float svx = 0.0f, svy = 0.0f;

    int n = 0;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;

            int nidx = ny * pitch + nx;
            float2 v = flowIn[nidx];
            float l = luma[nidx];

            float dl = l - lumaCenter;
            float rangeW = expf(-dl * dl * invRange2);

            float d2 = static_cast<float>(dx * dx + dy * dy) + 0.5f;
            float w = rangeW / d2;

            float fx = static_cast<float>(dx);
            float fy = static_cast<float>(dy);

            sw += w;
            sx += w * fx;
            sy += w * fy;
            sxx += w * fx * fx;
            syy += w * fy * fy;
            sxy += w * fx * fy;
            su += w * v.x;
            sv += w * v.y;
            sux += w * v.x * fx;
            suy += w * v.x * fy;
            svx += w * v.y * fx;
            svy += w * v.y * fy;

            ++n;
        }
    }

    if (n < 6 || sw < 1e-6f) {
        flowOut[idx] = flowIn[idx];
        return;
    }

    float invW = 1.0f / sw;
    float mx = sx * invW;
    float my = sy * invW;
    float mu = su * invW;
    float mv = sv * invW;

    float cxx = sxx * invW - mx * mx;
    float cyy = syy * invW - my * my;
    float cxy = sxy * invW - mx * my;

    float cux = sux * invW - mx * mu;
    float cuy = suy * invW - my * mu;
    float cvx = svx * invW - mx * mv;
    float cvy = svy * invW - my * mv;

    float det = cxx * cyy - cxy * cxy;

    float a1 = 0.0f, a2 = 0.0f, b1 = 0.0f, b2 = 0.0f;

    if (fabsf(det) > 1e-8f) {
        float invDet = 1.0f / det;
        a1 = (cyy * cux - cxy * cuy) * invDet;
        a2 = (cxx * cuy - cxy * cux) * invDet;
        b1 = (cyy * cvx - cxy * cvy) * invDet;
        b2 = (cxx * cvy - cxy * cvx) * invDet;
    }

    float grad2 = a1 * a1 + a2 * a2 + b1 * b1 + b2 * b2;
    float blend = expf(-grad2 / (2.0f * 0.05f * 0.05f));
    if (blend < 0.3f) blend = 0.3f;

    flowOut[idx].x = (1.0f - blend) * flowIn[idx].x + blend * mu;
    flowOut[idx].y = (1.0f - blend) * flowIn[idx].y + blend * mv;
}

// ============================================================================
// 散度/旋度诊断
// ============================================================================

__global__ void jellyDiagnosticKernel(
    const float2* __restrict__ flow,
    float* __restrict__ divergence,
    float* __restrict__ curl,
    int W, int H, int pitch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;

    float dudx = 0.0f, dvdy = 0.0f;
    float dudy = 0.0f, dvdx = 0.0f;

    if (x > 0 && x < W - 1) {
        dudx = 0.5f * (flow[idx + 1].x - flow[idx - 1].x);
        dvdx = 0.5f * (flow[idx + 1].y - flow[idx - 1].y);
    }
    if (y > 0 && y < H - 1) {
        dudy = 0.5f * (flow[idx + pitch].x - flow[idx - pitch].x);
        dvdy = 0.5f * (flow[idx + pitch].y - flow[idx - pitch].y);
    }

    divergence[idx] = dudx + dvdy;
    curl[idx] = dvdx - dudy;
}

// ============================================================================
// 果冻抑制
// ============================================================================

__global__ void jellySuppressionKernel(
    const float2* __restrict__ flowIn,
    const float* __restrict__ divergence,
    const float* __restrict__ curl,
    float2* __restrict__ flowOut,
    int W, int H, int pitch,
    float divThreshold,
    float curlThreshold,
    float strength)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float2 c = flowIn[idx];

    float div = divergence[idx];
    float crl = curl[idx];

    auto load = [&](int xx, int yy) -> float2 {
        xx = max(0, min(xx, W - 1));
        yy = max(0, min(yy, H - 1));
        return flowIn[yy * pitch + xx];
    };

    float2 uL = load(x - 1, y);
    float2 uR = load(x + 1, y);
    float2 uU = load(x, y - 1);
    float2 uD = load(x, y + 1);

    float uBar = 0.25f * (uL.x + uR.x + uU.x + uD.x);
    float vBar = 0.25f * (uL.y + uR.y + uU.y + uD.y);

    float divMag = fabsf(div);
    float curlMag = fabsf(crl);

    float jellyStrength = 0.0f;

    if (divMag > divThreshold) {
        jellyStrength += (divMag - divThreshold) / divThreshold;
    }
    if (curlMag > curlThreshold) {
        jellyStrength += (curlMag - curlThreshold) / curlThreshold;
    }

    jellyStrength = fminf(jellyStrength * strength, 1.0f);

    float2 r;
    r.x = (1.0f - jellyStrength) * c.x + jellyStrength * uBar;
    r.y = (1.0f - jellyStrength) * c.y + jellyStrength * vBar;

    flowOut[idx] = r;
}

// ============================================================================
// 时域锚定
// ============================================================================

__global__ void temporalAnchoringKernel(
    const float2* __restrict__ currentFlow,
    const float2* __restrict__ prevSmoothedFlow,
    float2* __restrict__ smoothedFlow,
    int W, int H, int pitch,
    float w)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * pitch + x;
    float2 cur = currentFlow[idx];

    float sx = static_cast<float>(x) + cur.x;
    float sy = static_cast<float>(y) + cur.y;

    float2 prevWarped = cur;

    if (sx >= 0.0f && sy >= 0.0f &&
        sx <= static_cast<float>(W - 1) &&
        sy <= static_cast<float>(H - 1))
    {
        int x0 = static_cast<int>(sx);
        int y0 = static_cast<int>(sy);
        int x1 = min(x0 + 1, W - 1);
        int y1 = min(y0 + 1, H - 1);
        float fx = sx - static_cast<float>(x0);
        float fy = sy - static_cast<float>(y0);

        float2 v00 = prevSmoothedFlow[y0 * pitch + x0];
        float2 v10 = prevSmoothedFlow[y0 * pitch + x1];
        float2 v01 = prevSmoothedFlow[y1 * pitch + x0];
        float2 v11 = prevSmoothedFlow[y1 * pitch + x1];

        float w00 = (1.0f - fx) * (1.0f - fy);
        float w10 = fx * (1.0f - fy);
        float w01 = (1.0f - fx) * fy;
        float w11 = fx * fy;

        prevWarped.x = w00 * v00.x + w10 * v10.x
                     + w01 * v01.x + w11 * v11.x;
        prevWarped.y = w00 * v00.y + w10 * v10.y
                     + w01 * v01.y + w11 * v11.y;
    }

    float dx = cur.x - prevWarped.x;
    float dy = cur.y - prevWarped.y;
    float diff2 = dx * dx + dy * dy;

    float consistency = expf(-diff2 / 2.0f);
    float wEff = w * consistency;

    smoothedFlow[idx].x = (1.0f - wEff) * cur.x + wEff * prevWarped.x;
    smoothedFlow[idx].y = (1.0f - wEff) * cur.y + wEff * prevWarped.y;
}

// ============================================================================
// 主机端接口
// ============================================================================

extern "C" {

bool ljLaunchAnisotropicCurvature(
    const float2* flowIn,
    float2* flowOut,
    const float* Jxx,
    const float* Jxy,
    const float* Jyy,
    int W, int H,
    float mu, float omega, float kappa,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    anisotropicCurvatureKernel<<<grid, block, 0, stream>>>(
        flowIn, flowOut, Jxx, Jxy, Jyy, W, H, W, mu, omega, kappa);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchQuadraticMotionFit(
    const float2* flowIn,
    float2* flowOut,
    const float* luma,
    int W, int H,
    int radius,
    float rangeSigma,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    quadraticMotionFitKernel<<<grid, block, 0, stream>>>(
        flowIn, flowOut, luma, W, H, W, radius, rangeSigma);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchJellyDiagnostic(
    const float2* flow,
    float* divergence,
    float* curl,
    int W, int H,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    jellyDiagnosticKernel<<<grid, block, 0, stream>>>(
        flow, divergence, curl, W, H, W);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchJellySuppression(
    const float2* flowIn,
    const float* divergence,
    const float* curl,
    float2* flowOut,
    int W, int H,
    float divThreshold,
    float curlThreshold,
    float strength,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    jellySuppressionKernel<<<grid, block, 0, stream>>>(
        flowIn, divergence, curl, flowOut, W, H, W,
        divThreshold, curlThreshold, strength);

    return cudaGetLastError() == cudaSuccess;
}

bool ljLaunchTemporalAnchoring(
    const float2* currentFlow,
    const float2* prevSmoothedFlow,
    float2* smoothedFlow,
    int W, int H,
    float w,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    temporalAnchoringKernel<<<grid, block, 0, stream>>>(
        currentFlow, prevSmoothedFlow, smoothedFlow, W, H, W, w);

    return cudaGetLastError() == cudaSuccess;
}

} // extern "C"

} // namespace Cuda
} // namespace Lingjing