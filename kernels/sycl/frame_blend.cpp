// ============================================================================
// kernels/sycl/frame_blend.cpp
// 灵境 Lingjing — SYCL 帧合成
// ============================================================================

#include <sycl/sycl.hpp>

namespace Lingjing {
    namespace Sycl {

        // ============================================================================
        // 遮挡感知合成
        // ============================================================================

        class OcclusionAwareBlendKernel {
        public:
            OcclusionAwareBlendKernel(
                const float* warpFrom1,
                const float* warpFrom0,
                const float* occProb,
                float* output,
                int W, int H, int pitch,
                float alpha)
                : w1_(warpFrom1), w0_(warpFrom0), occ_(occProb),
                out_(output), W_(W), H_(H), pitch_(pitch), alpha_(alpha) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                float p = occ_[idx];

                float baseW_fwd = 1.0f - alpha_;
                float bias = (1.0f - 2.0f * alpha_) * p;
                float w_fwd = baseW_fwd + bias;

                w_fwd = sycl::fmin(sycl::fmax(w_fwd, 0.0f), 1.0f);
                float w_bwd = 1.0f - w_fwd;

                out_[idx] = w_fwd * w0_[idx] + w_bwd * w1_[idx];
            }

        private:
            const float* w1_;
            const float* w0_;
            const float* occ_;
            float* out_;
            int W_, H_, pitch_;
            float alpha_;
        };

        // ============================================================================
        // 多帧合成
        // ============================================================================

        class MultiFrameBlendKernel {
        public:
            MultiFrameBlendKernel(
                const float* multiWarpFrom1,
                const float* multiWarpFrom0,
                const float* occProb,
                float* multiOutput,
                int W, int H, int pitch,
                int numFrames,
                float alphaStart,
                float alphaStep)
                : mw1_(multiWarpFrom1), mw0_(multiWarpFrom0),
                occ_(occProb), out_(multiOutput),
                W_(W), H_(H), pitch_(pitch),
                numFrames_(numFrames),
                alphaStart_(alphaStart), alphaStep_(alphaStep) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                float p = occ_[idx];
                int planeSize = W_ * H_;

                for (int n = 0; n < numFrames_; ++n) {
                    float alpha = alphaStart_ + static_cast<float>(n) * alphaStep_;

                    float baseW_fwd = 1.0f - alpha;
                    float bias = (1.0f - 2.0f * alpha) * p;
                    float w_fwd = baseW_fwd + bias;

                    w_fwd = sycl::fmin(sycl::fmax(w_fwd, 0.0f), 1.0f);
                    float w_bwd = 1.0f - w_fwd;

                    float a = mw1_[n * planeSize + idx];
                    float b = mw0_[n * planeSize + idx];

                    out_[n * planeSize + idx] = w_fwd * b + w_bwd * a;
                }
            }

        private:
            const float* mw1_;
            const float* mw0_;
            const float* occ_;
            float* out_;
            int W_, H_, pitch_;
            int numFrames_;
            float alphaStart_, alphaStep_;
        };

        // ============================================================================
        // 时域平滑
        // ============================================================================

        class TemporalSmoothKernel {
        public:
            TemporalSmoothKernel(
                const float* currentInterp,
                const float* prevInterp,
                const float* occProb,
                float* smoothed,
                int W, int H, int pitch,
                float historyWeight)
                : cur_(currentInterp), prev_(prevInterp),
                occ_(occProb), out_(smoothed),
                W_(W), H_(H), pitch_(pitch),
                histWeight_(historyWeight) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                float p = occ_[idx];
                float w = histWeight_ * (1.0f - p);

                out_[idx] = (1.0f - w) * cur_[idx] + w * prev_[idx];
            }

        private:
            const float* cur_;
            const float* prev_;
            const float* occ_;
            float* out_;
            int W_, H_, pitch_;
            float histWeight_;
        };

        // ============================================================================
        // 主机端接口
        // ============================================================================

        extern "C" {

            bool ljSyclLaunchOcclusionAwareBlend(
                sycl::queue& q,
                const float* warpFrom1,
                const float* warpFrom0,
                const float* occProb,
                float* output,
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
                            OcclusionAwareBlendKernel(
                                warpFrom1, warpFrom0, occProb, output,
                                W, H, W, alpha));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchMultiFrameBlend(
                sycl::queue& q,
                const float* multiWarpFrom1,
                const float* multiWarpFrom0,
                const float* occProb,
                float* multiOutput,
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
                            MultiFrameBlendKernel(
                                multiWarpFrom1, multiWarpFrom0,
                                occProb, multiOutput,
                                W, H, W, numFrames, alphaStart, alphaStep));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchTemporalSmooth(
                sycl::queue& q,
                const float* currentInterp,
                const float* prevInterp,
                const float* occProb,
                float* smoothed,
                int W, int H,
                float historyWeight)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            TemporalSmoothKernel(
                                currentInterp, prevInterp, occProb, smoothed,
                                W, H, W, historyWeight));
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