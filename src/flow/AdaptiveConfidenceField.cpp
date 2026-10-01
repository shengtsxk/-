#include "flow/AdaptiveConfidenceField.h"
#include "core/Logger.h"

#ifdef LJ_NVIDIA
#include <cuda_runtime.h>
#endif

#include <cmath>

namespace Lingjing {

    // ============================================================================
    // CUDA 内核
    // ============================================================================

#ifdef LJ_NVIDIA

    __global__ void bayesianFusionKernel(
        const float* __restrict__ geoFlow,
        const float* __restrict__ flowFlow,
        const float* __restrict__ geoConsistency,
        const float* __restrict__ flowConfidence,
        const float* __restrict__ depth,
        float* __restrict__ finalFlow,
        float* __restrict__ finalConf,
        int W, int H, int pitch,
        float geoBaseSigma,
        float flowBaseSigma)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= W || y >= H) return;

        int idx = y * pitch + x;

        float gc = geoConsistency[idx];
        float fc = flowConfidence[idx];

        float gw = gc / (geoBaseSigma * geoBaseSigma + 1e-8f);
        float fw = fc / (flowBaseSigma * flowBaseSigma + 1e-8f);

        // 深度梯度：几何可信度增强
        float d = depth[idx];
        float dGrad = 0.0f;

        if (x > 0 && x < W - 1) {
            dGrad += fabsf(depth[idx + 1] - depth[idx - 1]);
        }
        if (y > 0 && y < H - 1) {
            dGrad += fabsf(depth[idx + pitch] - depth[idx - pitch]);
        }

        float geoBoost = 1.0f + 2.0f * fminf(dGrad * 5.0f, 1.0f);
        gw *= geoBoost;

        float totalW = gw + fw + 1e-10f;

        finalFlow[idx * 2 + 0] = (gw * geoFlow[idx * 2 + 0] +
            fw * flowFlow[idx * 2 + 0]) / totalW;
        finalFlow[idx * 2 + 1] = (gw * geoFlow[idx * 2 + 1] +
            fw * flowFlow[idx * 2 + 1]) / totalW;

        finalConf[idx] = fminf(gw, fw) / totalW * 2.0f;
    }

    __global__ void temporalSmoothKernel(
        const float* __restrict__ currentFlow,
        const float* __restrict__ prevFlow,
        const float* __restrict__ flowConfidence,
        const float* __restrict__ depth,
        float* __restrict__ smoothedFlow,
        int W, int H, int pitch,
        float temporalWeight)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= W || y >= H) return;

        int idx = y * pitch + x;

        float cf = flowConfidence[idx];

        // 深度一致性
        float depthConsistency = 1.0f;

        if (x > 0 && x < W - 1) {
            float dDx = fabsf(depth[idx + 1] - depth[idx - 1]);
            depthConsistency *= expf(-dDx * 5.0f);
        }
        if (y > 0 && y < H - 1) {
            float dDy = fabsf(depth[idx + pitch] - depth[idx - pitch]);
            depthConsistency *= expf(-dDy * 5.0f);
        }

        float w = temporalWeight * cf * depthConsistency;
        if (w > 1.0f) w = 1.0f;
        if (w < 0.0f) w = 0.0f;

        smoothedFlow[idx * 2 + 0] = (1.0f - w) * currentFlow[idx * 2 + 0] +
            w * prevFlow[idx * 2 + 0];
        smoothedFlow[idx * 2 + 1] = (1.0f - w) * currentFlow[idx * 2 + 1] +
            w * prevFlow[idx * 2 + 1];
    }

#endif

    // ============================================================================
    // Impl
    // ============================================================================

    struct AdaptiveConfidenceField::Impl {
        GpuTexture cachedFinalFlow;
        GpuTexture cachedConfidence;
        GpuTexture cachedSmoothedFlow;
        uint32_t cachedWidth = 0;
        uint32_t cachedHeight = 0;
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    AdaptiveConfidenceField::AdaptiveConfidenceField()
        : impl_(std::make_unique<Impl>()) {
    }

    AdaptiveConfidenceField::~AdaptiveConfidenceField() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool AdaptiveConfidenceField::initialize(IGpuContext* gpuContext) {
        if (initialized_) return true;
        if (!gpuContext || !gpuContext->isValid()) return false;

        gpuContext_ = gpuContext;
        initialized_ = true;

        LOG_INFO("AdaptiveConfidenceField initialized");
        return true;
    }

    void AdaptiveConfidenceField::shutdown() {
        initialized_ = false;
        gpuContext_ = nullptr;
    }

    // ============================================================================
    // 融合
    // ============================================================================

    bool AdaptiveConfidenceField::fuse(
        const GpuTexture& geoFlow,
        const GpuTexture& geoConsistency,
        const GpuTexture& flowFlow,
        const GpuTexture& flowConfidence,
        const GpuTexture& depth,
        AdaptiveConfidenceResult& result)
    {
        if (!initialized_) return false;

        if (!geoFlow.valid() || !flowFlow.valid() || !depth.valid()) {
            LOG_ERROR("AdaptiveConfidenceField: invalid inputs");
            return false;
        }

        uint32_t W = geoFlow.width;
        uint32_t H = geoFlow.height;

        if (impl_->cachedWidth != W || impl_->cachedHeight != H) {
            auto flowBuf = gpuContext_->createTexture(W, H,
                TextureFormat::R32G32_FLOAT);
            auto confBuf = gpuContext_->createTexture(W, H,
                TextureFormat::R32_FLOAT);

            if (!flowBuf || !confBuf) return false;

            impl_->cachedFinalFlow.nativeHandle = flowBuf->nativeHandle();
            impl_->cachedFinalFlow.width = W;
            impl_->cachedFinalFlow.height = H;
            impl_->cachedFinalFlow.format = TextureFormat::R32G32_FLOAT;

            impl_->cachedConfidence.nativeHandle = confBuf->nativeHandle();
            impl_->cachedConfidence.width = W;
            impl_->cachedConfidence.height = H;
            impl_->cachedConfidence.format = TextureFormat::R32_FLOAT;

            impl_->cachedWidth = W;
            impl_->cachedHeight = H;
        }

#ifdef LJ_NVIDIA
        dim3 block(16, 16);
        dim3 grid((W + 15) / 16, (H + 15) / 16);

        bayesianFusionKernel << <grid, block >> > (
            static_cast<const float*>(geoFlow.nativeHandle),
            static_cast<const float*>(flowFlow.nativeHandle),
            static_cast<const float*>(geoConsistency.nativeHandle),
            static_cast<const float*>(flowConfidence.nativeHandle),
            static_cast<const float*>(depth.nativeHandle),
            static_cast<float*>(impl_->cachedFinalFlow.nativeHandle),
            static_cast<float*>(impl_->cachedConfidence.nativeHandle),
            static_cast<int>(W), static_cast<int>(H), static_cast<int>(W),
            0.5f, 1.0f);

        cudaError_t err = cudaGetLastError();
        if (err != cudaSuccess) {
            LOG_ERROR("Fusion kernel failed: %s", cudaGetErrorString(err));
            return false;
        }
#endif

        result.finalFlow = impl_->cachedFinalFlow;
        result.confidence = impl_->cachedConfidence;
        result.width = W;
        result.height = H;

        return true;
    }

    // ============================================================================
    // 时域平滑
    // ============================================================================

    bool AdaptiveConfidenceField::temporalSmooth(
        const GpuTexture& currentFlow,
        const GpuTexture& prevFlow,
        const GpuTexture& flowConfidence,
        const GpuTexture& depth,
        GpuTexture& outFlow,
        float temporalWeight)
    {
        if (!initialized_) return false;
        if (!currentFlow.valid() || !prevFlow.valid()) return false;

        uint32_t W = currentFlow.width;
        uint32_t H = currentFlow.height;

#ifdef LJ_NVIDIA
        dim3 block(16, 16);
        dim3 grid((W + 15) / 16, (H + 15) / 16);

        temporalSmoothKernel << <grid, block >> > (
            static_cast<const float*>(currentFlow.nativeHandle),
            static_cast<const float*>(prevFlow.nativeHandle),
            static_cast<const float*>(flowConfidence.nativeHandle),
            static_cast<const float*>(depth.nativeHandle),
            static_cast<float*>(outFlow.nativeHandle),
            static_cast<int>(W), static_cast<int>(H), static_cast<int>(W),
            temporalWeight);

        return cudaGetLastError() == cudaSuccess;
#else
        return false;
#endif
    }

} // namespace Lingjing