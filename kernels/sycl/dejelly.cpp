// ============================================================================
// kernels/sycl/dejelly.cpp
// 灵境 Lingjing — SYCL 去果冻
// ============================================================================

#include <sycl/sycl.hpp>

namespace Lingjing {
    namespace Sycl {

        // ============================================================================
        // 各向异性曲率惩罚
        // ============================================================================

        class AnisotropicCurvatureKernel {
        public:
            AnisotropicCurvatureKernel(
                const sycl::float2* flowIn,
                sycl::float2* flowOut,
                const float* Jxx,
                const float* Jxy,
                const float* Jyy,
                int W, int H, int pitch,
                float mu, float omega, float kappa)
                : in_(flowIn), out_(flowOut),
                Jxx_(Jxx), Jxy_(Jxy), Jyy_(Jyy),
                W_(W), H_(H), pitch_(pitch),
                mu_(mu), omega_(omega), kappa_(kappa) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                sycl::float2 c = in_[idx];

                float jxx = Jxx_[idx];
                float jxy = Jxy_[idx];
                float jyy = Jyy_[idx];

                float trace = jxx + jyy;
                float diff = jxx - jyy;
                float disc = sycl::sqrt(diff * diff + 4.0f * jxy * jxy);

                float lam1 = 0.5f * (trace + disc);
                float lam2 = 0.5f * (trace - disc);

                float mu1 = 1.0f / sycl::sqrt(1.0f + lam1 / (kappa_ * kappa_));
                float mu2 = 1.0f / sycl::sqrt(1.0f + lam2 / (kappa_ * kappa_));

                float theta = 0.5f * sycl::atan2(2.0f * jxy, diff);
                float e1x = sycl::cos(theta);
                float e1y = sycl::sin(theta);
                float e2x = -e1y;
                float e2y = e1x;

                float Dxx = mu1 * e1x * e1x + mu2 * e2x * e2x;
                float Dxy = mu1 * e1x * e1y + mu2 * e2x * e2y;
                float Dyy = mu1 * e1y * e1y + mu2 * e2y * e2y;

                auto load = [&](int xx, int yy) -> sycl::float2 {
                    xx = sycl::max(0, sycl::min(xx, W_ - 1));
                    yy = sycl::max(0, sycl::min(yy, H_ - 1));
                    return in_[yy * pitch_ + xx];
                    };

                sycl::float2 uL = load(x - 1, y);
                sycl::float2 uR = load(x + 1, y);
                sycl::float2 uU = load(x, y - 1);
                sycl::float2 uD = load(x, y + 1);
                sycl::float2 uLL = load(x - 2, y);
                sycl::float2 uRR = load(x + 2, y);
                sycl::float2 uUU = load(x, y - 2);
                sycl::float2 uDD = load(x, y + 2);

                float uxx_u = uLL.x() - 2.0f * uL.x() + c.x();
                float uxx_b = c.x() - 2.0f * uR.x() + uRR.x();
                float uxx = 0.5f * (uxx_u + uxx_b);

                float uyy_u = uUU.x() - 2.0f * uU.x() + c.x();
                float uyy_b = c.x() - 2.0f * uD.x() + uDD.x();
                float uyy = 0.5f * (uyy_u + uyy_b);

                float vxx_u = uLL.y() - 2.0f * uL.y() + c.y();
                float vxx_b = c.y() - 2.0f * uR.y() + uRR.y();
                float vxx = 0.5f * (vxx_u + vxx_b);

                float vyy_u = uUU.y() - 2.0f * uU.y() + c.y();
                float vyy_b = c.y() - 2.0f * uD.y() + uDD.y();
                float vyy = 0.5f * (vyy_u + vyy_b);

                float du = mu_ * (Dxx * uxx + Dyy * uyy);
                float dv = mu_ * (Dxx * vxx + Dyy * vyy);

                sycl::float2 r;
                r.x() = (1.0f - omega_) * c.x() + omega_ * (c.x() - du);
                r.y() = (1.0f - omega_) * c.y() + omega_ * (c.y() - dv);

                out_[idx] = r;
            }

        private:
            const sycl::float2* in_;
            sycl::float2* out_;
            const float* Jxx_;
            const float* Jxy_;
            const float* Jyy_;
            int W_, H_, pitch_;
            float mu_, omega_, kappa_;
        };

        // ============================================================================
        // 果冻诊断
        // ============================================================================

        class JellyDiagnosticKernel {
        public:
            JellyDiagnosticKernel(
                const sycl::float2* flow,
                float* divergence,
                float* curl,
                int W, int H, int pitch)
                : flow_(flow), div_(divergence), curl_(curl),
                W_(W), H_(H), pitch_(pitch) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;

                float dudx = 0.0f, dvdy = 0.0f;
                float dudy = 0.0f, dvdx = 0.0f;

                if (x > 0 && x < W_ - 1) {
                    dudx = 0.5f * (flow_[idx + 1].x() - flow_[idx - 1].x());
                    dvdx = 0.5f * (flow_[idx + 1].y() - flow_[idx - 1].y());
                }
                if (y > 0 && y < H_ - 1) {
                    dudy = 0.5f * (flow_[idx + pitch_].x() - flow_[idx - pitch_].x());
                    dvdy = 0.5f * (flow_[idx + pitch_].y() - flow_[idx - pitch_].y());
                }

                div_[idx] = dudx + dvdy;
                curl_[idx] = dvdx - dudy;
            }

        private:
            const sycl::float2* flow_;
            float* div_;
            float* curl_;
            int W_, H_, pitch_;
        };

        // ============================================================================
        // 果冻抑制
        // ============================================================================

        class JellySuppressionKernel {
        public:
            JellySuppressionKernel(
                const sycl::float2* flowIn,
                const float* divergence,
                const float* curl,
                sycl::float2* flowOut,
                int W, int H, int pitch,
                float divThreshold,
                float curlThreshold,
                float strength)
                : in_(flowIn), div_(divergence), curl_(curl), out_(flowOut),
                W_(W), H_(H), pitch_(pitch),
                divThreshold_(divThreshold),
                curlThreshold_(curlThreshold),
                strength_(strength) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                int idx = y * pitch_ + x;
                sycl::float2 c = in_[idx];

                float div = div_[idx];
                float crl = curl_[idx];

                auto load = [&](int xx, int yy) -> sycl::float2 {
                    xx = sycl::max(0, sycl::min(xx, W_ - 1));
                    yy = sycl::max(0, sycl::min(yy, H_ - 1));
                    return in_[yy * pitch_ + xx];
                    };

                sycl::float2 uL = load(x - 1, y);
                sycl::float2 uR = load(x + 1, y);
                sycl::float2 uU = load(x, y - 1);
                sycl::float2 uD = load(x, y + 1);

                float uBar = 0.25f * (uL.x() + uR.x() + uU.x() + uD.x());
                float vBar = 0.25f * (uL.y() + uR.y() + uU.y() + uD.y());

                float divMag = sycl::fabs(div);
                float curlMag = sycl::fabs(crl);

                float jellyStrength = 0.0f;

                if (divMag > divThreshold_) {
                    jellyStrength += (divMag - divThreshold_) / divThreshold_;
                }
                if (curlMag > curlThreshold_) {
                    jellyStrength += (curlMag - curlThreshold_) / curlThreshold_;
                }

                jellyStrength = sycl::fmin(jellyStrength * strength_, 1.0f);

                sycl::float2 r;
                r.x() = (1.0f - jellyStrength) * c.x() + jellyStrength * uBar;
                r.y() = (1.0f - jellyStrength) * c.y() + jellyStrength * vBar;

                out_[idx] = r;
            }

        private:
            const sycl::float2* in_;
            const float* div_;
            const float* curl_;
            sycl::float2* out_;
            int W_, H_, pitch_;
            float divThreshold_, curlThreshold_, strength_;
        };

        // ============================================================================
        // 主机端接口
        // ============================================================================

        extern "C" {

            bool ljSyclLaunchAnisotropicCurvature(
                sycl::queue& q,
                const sycl::float2* flowIn,
                sycl::float2* flowOut,
                const float* Jxx,
                const float* Jxy,
                const float* Jyy,
                int W, int H,
                float mu, float omega, float kappa)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            AnisotropicCurvatureKernel(
                                flowIn, flowOut, Jxx, Jxy, Jyy,
                                W, H, W, mu, omega, kappa));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchJellyDiagnostic(
                sycl::queue& q,
                const sycl::float2* flow,
                float* divergence,
                float* curl,
                int W, int H)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            JellyDiagnosticKernel(flow, divergence, curl, W, H, W));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchJellySuppression(
                sycl::queue& q,
                const sycl::float2* flowIn,
                const float* divergence,
                const float* curl,
                sycl::float2* flowOut,
                int W, int H,
                float divThreshold,
                float curlThreshold,
                float strength)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            JellySuppressionKernel(
                                flowIn, divergence, curl, flowOut,
                                W, H, W,
                                divThreshold, curlThreshold, strength));
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