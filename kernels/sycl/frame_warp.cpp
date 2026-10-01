// ============================================================================
// kernels/sycl/frame_warp.cpp
// 灵境 Lingjing — SYCL 帧 warp
// ============================================================================

#include <sycl/sycl.hpp>

namespace Lingjing {
    namespace Sycl {

        // ============================================================================
        // 三次权重
        // ============================================================================

        inline float cubicWeight(float x, float a) {
            float ax = sycl::fabs(x);
            if (ax <= 1.0f) {
                return (a + 2.0f) * ax * ax * ax
                    - (a + 3.0f) * ax * ax + 1.0f;
            }
            else if (ax < 2.0f) {
                return a * ax * ax * ax
                    - 5.0f * a * ax * ax
                    + 8.0f * a * ax - 4.0f * a;
            }
            return 0.0f;
        }

        inline float bicubicSample(
            const float* img,
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
            float sum = 0.0f, wsum = 0.0f;

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

        class BidirectionalWarpKernel {
        public:
            BidirectionalWarpKernel(
                const float* I0,
                const float* I1,
                const sycl::float2* fwdFlow,
                const sycl::float2* bwdFlow,
                float* warpFrom1,
                float* warpFrom0,
                int W, int H, int pitch,
                float alpha)
                : I0_(I0), I1_(I1),
                fwd_(fwdFlow), bwd_(bwdFlow),
                warpFrom1_(warpFrom1), warpFrom0_(warpFrom0),
                W_(W), H_(H), pitch_(pitch), alpha_(alpha) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;

                sycl::float2 bf = bwd_[idx];
                float fwx = static_cast<float>(x) + alpha_ * bf.x();
                float fwy = static_cast<float>(y) + alpha_ * bf.y();
                warpFrom1_[idx] = bicubicSample(I1_, W_, H_, pitch_, fwx, fwy);

                sycl::float2 ff = fwd_[idx];
                float bwx = static_cast<float>(x) + (1.0f - alpha_) * ff.x();
                float bwy = static_cast<float>(y) + (1.0f - alpha_) * ff.y();
                warpFrom0_[idx] = bicubicSample(I0_, W_, H_, pitch_, bwx, bwy);
            }

        private:
            const float* I0_;
            const float* I1_;
            const sycl::float2* fwd_;
            const sycl::float2* bwd_;
            float* warpFrom1_;
            float* warpFrom0_;
            int W_, H_, pitch_;
            float alpha_;
        };

        // ============================================================================
        // 多帧 warp 内核
        // ============================================================================

        class MultiFrameWarpKernel {
        public:
            MultiFrameWarpKernel(
                const float* I0,
                const float* I1,
                const sycl::float2* fwdFlow,
                const sycl::float2* bwdFlow,
                float* multiWarpFrom1,
                float* multiWarpFrom0,
                int W, int H, int pitch,
                int numFrames,
                float alphaStart,
                float alphaStep)
                : I0_(I0), I1_(I1),
                fwd_(fwdFlow), bwd_(bwdFlow),
                mw1_(multiWarpFrom1), mw0_(multiWarpFrom0),
                W_(W), H_(H), pitch_(pitch),
                numFrames_(numFrames),
                alphaStart_(alphaStart), alphaStep_(alphaStep) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                int planeSize = W_ * H_;

                sycl::float2 ff = fwd_[idx];
                sycl::float2 bf = bwd_[idx];

                float fxBase = static_cast<float>(x);
                float fyBase = static_cast<float>(y);

                for (int n = 0; n < numFrames_; ++n) {
                    float alpha = alphaStart_ + static_cast<float>(n) * alphaStep_;

                    float fwx = fxBase + alpha * bf.x();
                    float fwy = fyBase + alpha * bf.y();
                    mw1_[n * planeSize + idx] =
                        bicubicSample(I1_, W_, H_, pitch_, fwx, fwy);

                    float bwx = fxBase + (1.0f - alpha) * ff.x();
                    float bwy = fyBase + (1.0f - alpha) * ff.y();
                    mw0_[n * planeSize + idx] =
                        bicubicSample(I0_, W_, H_, pitch_, bwx, bwy);
                }
            }

        private:
            const float* I0_;
            const float* I1_;
            const sycl::float2* fwd_;
            const sycl::float2* bwd_;
            float* mw1_;
            float* mw0_;
            int W_, H_, pitch_;
            int numFrames_;
            float alphaStart_, alphaStep_;
        };

        // ============================================================================
        // 主机端接口
        // ============================================================================

        extern "C" {

            bool ljSyclLaunchBidirectionalWarp(
                sycl::queue& q,
                const float* I0,
                const float* I1,
                const sycl::float2* fwdFlow,
                const sycl::float2* bwdFlow,
                float* warpFrom1,
                float* warpFrom0,
                int W, int H,
                float alpha)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            BidirectionalWarpKernel(
                                I0, I1, fwdFlow, bwdFlow,
                                warpFrom1, warpFrom0,
                                W, H, W, alpha));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchMultiFrameWarp(
                sycl::queue& q,
                const float* I0,
                const float* I1,
                const sycl::float2* fwdFlow,
                const sycl::float2* bwdFlow,
                float* multiWarpFrom1,
                float* multiWarpFrom0,
                int W, int H,
                int numFrames,
                float alphaStart,
                float alphaStep)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            MultiFrameWarpKernel(
                                I0, I1, fwdFlow, bwdFlow,
                                multiWarpFrom1, multiWarpFrom0,
                                W, H, W,
                                numFrames, alphaStart, alphaStep));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

        } // extern "C"

    } // namespace Sycl
} // namespace Lingjing