// ============================================================================
// kernels/sycl/flow_consistency.cpp
// 灵境 Lingjing — SYCL 一致性校验与遮挡推断
// ============================================================================

#include <sycl/sycl.hpp>

namespace Lingjing {
    namespace Sycl {

        // ============================================================================
        // 前向-后向一致性
        // ============================================================================

        class ForwardBackwardConsistencyKernel {
        public:
            ForwardBackwardConsistencyKernel(
                const sycl::float2* fwdFlow,
                const sycl::float2* bwdFlow,
                float* errorMap,
                int W, int H, int pitch)
                : fwd_(fwdFlow), bwd_(bwdFlow), error_(errorMap),
                W_(W), H_(H), pitch_(pitch) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                sycl::float2 wf = fwd_[idx];

                float sx = static_cast<float>(x) + wf.x();
                float sy = static_cast<float>(y) + wf.y();

                if (sx < 0.0f || sy < 0.0f ||
                    sx > static_cast<float>(W_ - 1) ||
                    sy > static_cast<float>(H_ - 1))
                {
                    error_[idx] = 1e6f;
                    return;
                }

                int x0 = static_cast<int>(sx);
                int y0 = static_cast<int>(sy);
                int x1 = sycl::min(x0 + 1, W_ - 1);
                int y1 = sycl::min(y0 + 1, H_ - 1);
                float fx = sx - static_cast<float>(x0);
                float fy = sy - static_cast<float>(y0);

                sycl::float2 b00 = bwd_[y0 * pitch_ + x0];
                sycl::float2 b10 = bwd_[y0 * pitch_ + x1];
                sycl::float2 b01 = bwd_[y1 * pitch_ + x0];
                sycl::float2 b11 = bwd_[y1 * pitch_ + x1];

                float w00 = (1.0f - fx) * (1.0f - fy);
                float w10 = fx * (1.0f - fy);
                float w01 = (1.0f - fx) * fy;
                float w11 = fx * fy;

                float bx = w00 * b00.x() + w10 * b10.x()
                    + w01 * b01.x() + w11 * b11.x();
                float by = w00 * b00.y() + w10 * b10.y()
                    + w01 * b01.y() + w11 * b11.y();

                float ex = wf.x() + bx;
                float ey = wf.y() + by;

                error_[idx] = sycl::sqrt(ex * ex + ey * ey);
            }

        private:
            const sycl::float2* fwd_;
            const sycl::float2* bwd_;
            float* error_;
            int W_, H_, pitch_;
        };

        // ============================================================================
        // 遮挡概率
        // ============================================================================

        class OcclusionProbabilityKernel {
        public:
            OcclusionProbabilityKernel(
                const float* errorMap,
                float* occProb,
                int W, int H, int pitch,
                float tau, float alpha)
                : error_(errorMap), prob_(occProb),
                W_(W), H_(H), pitch_(pitch),
                tau_(tau), alpha_(alpha) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                float e = error_[idx];

                float logit = alpha_ * (e / tau_ - 1.0f);
                prob_[idx] = 1.0f / (1.0f + sycl::exp(-logit));
            }

        private:
            const float* error_;
            float* prob_;
            int W_, H_, pitch_;
            float tau_, alpha_;
        };

        // ============================================================================
        // 边界硬化
        // ============================================================================

        class BoundaryHardeningKernel {
        public:
            BoundaryHardeningKernel(
                const float* occProb,
                float* occSharp,
                int W, int H, int pitch,
                float gradThreshold)
                : in_(occProb), out_(occSharp),
                W_(W), H_(H), pitch_(pitch),
                gradThreshold_(gradThreshold) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;

                float gx = 0.0f, gy = 0.0f;
                if (x > 0 && x < W_ - 1) {
                    gx = 0.5f * (in_[idx + 1] - in_[idx - 1]);
                }
                if (y > 0 && y < H_ - 1) {
                    gy = 0.5f * (in_[idx + pitch_] - in_[idx - pitch_]);
                }

                float gmag = sycl::sqrt(gx * gx + gy * gy);
                float p = in_[idx];

                if (gmag > gradThreshold_) {
                    float sharpened = (p > 0.5f) ? 1.0f : 0.0f;
                    out_[idx] = 0.7f * p + 0.3f * sharpened;
                }
                else {
                    out_[idx] = p;
                }
            }

        private:
            const float* in_;
            float* out_;
            int W_, H_, pitch_;
            float gradThreshold_;
        };

        // ============================================================================
        // 局部标准差
        // ============================================================================

        class LocalFlowStdKernel {
        public:
            LocalFlowStdKernel(
                const sycl::float2* flow,
                float* localStd,
                int W, int H, int pitch,
                int radius)
                : flow_(flow), out_(localStd),
                W_(W), H_(H), pitch_(pitch),
                radius_(radius) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                float sumU = 0.0f, sumV = 0.0f;
                float sumU2 = 0.0f, sumV2 = 0.0f;
                int n = 0;

                for (int dy = -radius_; dy <= radius_; ++dy) {
                    for (int dx = -radius_; dx <= radius_; ++dx) {
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx < 0 || nx >= W_ || ny < 0 || ny >= H_) continue;

                        sycl::float2 v = flow_[ny * pitch_ + nx];
                        sumU += v.x();
                        sumV += v.y();
                        sumU2 += v.x() * v.x();
                        sumV2 += v.y() * v.y();
                        ++n;
                    }
                }

                int idx = y * pitch_ + x;

                if (n < 2) {
                    out_[idx] = 1.0f;
                    return;
                }

                float invN = 1.0f / static_cast<float>(n);
                float meanU = sumU * invN;
                float meanV = sumV * invN;
                float varU = sumU2 * invN - meanU * meanU;
                float varV = sumV2 * invN - meanV * meanV;

                out_[idx] = sycl::sqrt(sycl::fmax(varU + varV, 1e-6f));
            }

        private:
            const sycl::float2* flow_;
            float* out_;
            int W_, H_, pitch_;
            int radius_;
        };

        // ============================================================================
        // 主机端接口
        // ============================================================================

        extern "C" {

            bool ljSyclLaunchConsistency(
                sycl::queue& q,
                const sycl::float2* fwdFlow,
                const sycl::float2* bwdFlow,
                float* errorMap,
                int W, int H)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            ForwardBackwardConsistencyKernel(
                                fwdFlow, bwdFlow, errorMap, W, H, W));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchOcclusionProbability(
                sycl::queue& q,
                const float* errorMap,
                float* occProb,
                int W, int H,
                float tau, float alpha)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            OcclusionProbabilityKernel(
                                errorMap, occProb, W, H, W, tau, alpha));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchBoundaryHardening(
                sycl::queue& q,
                const float* occProb,
                float* occSharp,
                int W, int H,
                float gradThreshold)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            BoundaryHardeningKernel(
                                occProb, occSharp, W, H, W, gradThreshold));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchLocalFlowStd(
                sycl::queue& q,
                const sycl::float2* flow,
                float* localStd,
                int W, int H,
                int radius)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            LocalFlowStdKernel(flow, localStd, W, H, W, radius));
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