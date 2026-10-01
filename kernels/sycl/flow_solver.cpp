// ============================================================================
// kernels/sycl/flow_solver.cpp
// 灵境 Lingjing — SYCL 变分光流求解器
// ============================================================================

#include <sycl/sycl.hpp>
#include <cstdint>
#include <vector>

namespace Lingjing {
    namespace Sycl {

        // ============================================================================
        // 常量
        // ============================================================================

        static constexpr float kEpsCbn = 1e-3f;

        // ============================================================================
        // 双线性采样
        // ============================================================================

        inline float bilinearSample(
            const float* img,
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

        inline sycl::float2 bilinearSampleFlow(
            const sycl::float2* flow,
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

            sycl::float2 a = flow[y0 * pitch + x0];
            sycl::float2 b = flow[y0 * pitch + x1];
            sycl::float2 c = flow[y1 * pitch + x0];
            sycl::float2 d = flow[y1 * pitch + x1];

            sycl::float2 r;
            r.x() = (1.0f - fx) * (1.0f - fy) * a.x()
                + fx * (1.0f - fy) * b.x()
                + (1.0f - fx) * fy * c.x()
                + fx * fy * d.x();
            r.y() = (1.0f - fx) * (1.0f - fy) * a.y()
                + fx * (1.0f - fy) * b.y()
                + (1.0f - fx) * fy * c.y()
                + fx * fy * d.y();
            return r;
        }

        // ============================================================================
        // SYCL 红黑 SOR 内核
        // ============================================================================

        class RbSORKernel {
        public:
            RbSORKernel(
                sycl::float2* fwdFlow,
                sycl::float2* bwdFlow,
                const float* I0,
                const float* I1,
                int W, int H, int pitch,
                float lambda, float omega, float gamma,
                int parity)
                : fwdFlow_(fwdFlow), bwdFlow_(bwdFlow),
                I0_(I0), I1_(I1),
                W_(W), H_(H), pitch_(pitch),
                lambda_(lambda), omega_(omega), gamma_(gamma),
                parity_(parity) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);

                if (x >= W_ || y >= H_) return;

                bool active = (((x + y) & 1) == parity_);
                if (!active) return;

                int idx = y * pitch_ + x;

                sycl::float2 wf = fwdFlow_[idx];
                sycl::float2 wb = bwdFlow_[idx];

                // 邻域
                int xL = sycl::max(x - 1, 0);
                int xR = sycl::min(x + 1, W_ - 1);
                int yU = sycl::max(y - 1, 0);
                int yD = sycl::min(y + 1, H_ - 1);

                sycl::float2 fL = fwdFlow_[y * pitch_ + xL];
                sycl::float2 fR = fwdFlow_[y * pitch_ + xR];
                sycl::float2 fU = fwdFlow_[yU * pitch_ + x];
                sycl::float2 fD = fwdFlow_[yD * pitch_ + x];

                // 前向流
                float uBarF = 0.25f * (fL.x() + fR.x() + fU.x() + fD.x());
                float vBarF = 0.25f * (fL.y() + fR.y() + fU.y() + fD.y());

                float wfx = static_cast<float>(x) + wf.x();
                float wfy = static_cast<float>(y) + wf.y();

                float I1w = bilinearSample(I1_, W_, H_, pitch_, wfx, wfy);
                float IzF = I1w - I0_[idx];

                float IxF = 0.5f * (
                    bilinearSample(I1_, W_, H_, pitch_, wfx + 1.0f, wfy) -
                    bilinearSample(I1_, W_, H_, pitch_, wfx - 1.0f, wfy));
                float IyF = 0.5f * (
                    bilinearSample(I1_, W_, H_, pitch_, wfx, wfy + 1.0f) -
                    bilinearSample(I1_, W_, H_, pitch_, wfx, wfy - 1.0f));

                float psiDF = IzF / sycl::sqrt(IzF * IzF + kEpsCbn * kEpsCbn);

                float dfLx = wf.x() - fL.x();
                float dfLy = wf.y() - fL.y();
                float dfRx = fR.x() - wf.x();
                float dfRy = fR.y() - wf.y();
                float dfUx = wf.x() - fU.x();
                float dfUy = wf.y() - fU.y();
                float dfDx = fD.x() - wf.x();
                float dfDy = fD.y() - wf.y();

                float gradF = 0.25f * (dfLx * dfLx + dfLy * dfLy
                    + dfRx * dfRx + dfRy * dfRy
                    + dfUx * dfUx + dfUy * dfUy
                    + dfDx * dfDx + dfDy * dfDy)
                    + kEpsCbn * kEpsCbn;

                float psiSF = 1.0f / (2.0f * sycl::sqrt(gradF));

                sycl::float2 bwdAt = bilinearSampleFlow(bwdFlow_, W_, H_, pitch_,
                    wfx, wfy);
                float symErrFx = wf.x() + bwdAt.x();
                float symErrFy = wf.y() + bwdAt.y();

                float denomF = 4.0f * lambda_ * psiSF + 1e-6f;

                float uNewF = uBarF -
                    (psiDF * IxF + 2.0f * gamma_ * symErrFx) / denomF;
                float vNewF = vBarF -
                    (psiDF * IyF + 2.0f * gamma_ * symErrFy) / denomF;

                fwdFlow_[idx].x() = (1.0f - omega_) * wf.x() + omega_ * uNewF;
                fwdFlow_[idx].y() = (1.0f - omega_) * wf.y() + omega_ * vNewF;

                // 反向流
                sycl::float2 bL = bwdFlow_[y * pitch_ + xL];
                sycl::float2 bR = bwdFlow_[y * pitch_ + xR];
                sycl::float2 bU = bwdFlow_[yU * pitch_ + x];
                sycl::float2 bD = bwdFlow_[yD * pitch_ + x];

                float uBarB = 0.25f * (bL.x() + bR.x() + bU.x() + bD.x());
                float vBarB = 0.25f * (bL.y() + bR.y() + bU.y() + bD.y());

                float wbx = static_cast<float>(x) + wb.x();
                float wby = static_cast<float>(y) + wb.y();

                float I0w = bilinearSample(I0_, W_, H_, pitch_, wbx, wby);
                float IzB = I0w - I1_[idx];

                float IxB = 0.5f * (
                    bilinearSample(I0_, W_, H_, pitch_, wbx + 1.0f, wby) -
                    bilinearSample(I0_, W_, H_, pitch_, wbx - 1.0f, wby));
                float IyB = 0.5f * (
                    bilinearSample(I0_, W_, H_, pitch_, wbx, wby + 1.0f) -
                    bilinearSample(I0_, W_, H_, pitch_, wbx, wby - 1.0f));

                float psiDB = IzB / sycl::sqrt(IzB * IzB + kEpsCbn * kEpsCbn);

                float dbLx = wb.x() - bL.x();
                float dbLy = wb.y() - bL.y();
                float dbRx = bR.x() - wb.x();
                float dbRy = bR.y() - wb.y();
                float dbUx = wb.x() - bU.x();
                float dbUy = wb.y() - bU.y();
                float dbDx = bD.x() - wb.x();
                float dbDy = bD.y() - wb.y();

                float gradB = 0.25f * (dbLx * dbLx + dbLy * dbLy
                    + dbRx * dbRx + dbRy * dbRy
                    + dbUx * dbUx + dbUy * dbUy
                    + dbDx * dbDx + dbDy * dbDy)
                    + kEpsCbn * kEpsCbn;

                float psiSB = 1.0f / (2.0f * sycl::sqrt(gradB));

                sycl::float2 fwdAt = bilinearSampleFlow(fwdFlow_, W_, H_, pitch_,
                    wbx, wby);
                float symErrBx = wb.x() + fwdAt.x();
                float symErrBy = wb.y() + fwdAt.y();

                float denomB = 4.0f * lambda_ * psiSB + 1e-6f;

                float uNewB = uBarB -
                    (psiDB * IxB + 2.0f * gamma_ * symErrBx) / denomB;
                float vNewB = vBarB -
                    (psiDB * IyB + 2.0f * gamma_ * symErrBy) / denomB;

                bwdFlow_[idx].x() = (1.0f - omega_) * wb.x() + omega_ * uNewB;
                bwdFlow_[idx].y() = (1.0f - omega_) * wb.y() + omega_ * vNewB;
            }

        private:
            sycl::float2* fwdFlow_;
            sycl::float2* bwdFlow_;
            const float* I0_;
            const float* I1_;
            int W_, H_, pitch_;
            float lambda_, omega_, gamma_;
            int parity_;
        };

        // ============================================================================
        // 金字塔下采样内核
        // ============================================================================

        class PyramidDownHKernel {
        public:
            PyramidDownHKernel(
                const float* src, float* dst,
                int srcW, int srcH, int srcPitch,
                int dstW, int dstH, int dstPitch)
                : src_(src), dst_(dst),
                srcW_(srcW), srcH_(srcH), srcPitch_(srcPitch),
                dstW_(dstW), dstH_(dstH), dstPitch_(dstPitch) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= dstW_ || y >= dstH_) return;

                int cx = x << 1;
                const float* row = src_ + (y << 1) * srcPitch_;

                float a = (cx - 2 >= 0) ? row[cx - 2] : row[0];
                float b = (cx - 1 >= 0) ? row[cx - 1] : row[0];
                float c = row[cx];
                float d = (cx + 1 < srcW_) ? row[cx + 1] : row[srcW_ - 1];
                float e = (cx + 2 < srcW_) ? row[cx + 2] : row[srcW_ - 1];

                dst_[y * dstPitch_ + x] =
                    (a + 4.0f * b + 6.0f * c + 4.0f * d + e) * (1.0f / 16.0f);
            }

        private:
            const float* src_;
            float* dst_;
            int srcW_, srcH_, srcPitch_;
            int dstW_, dstH_, dstPitch_;
        };

        class PyramidDownVKernel {
        public:
            PyramidDownVKernel(
                const float* src, float* dst,
                int srcW, int srcH, int srcPitch,
                int dstW, int dstH, int dstPitch)
                : src_(src), dst_(dst),
                srcW_(srcW), srcH_(srcH), srcPitch_(srcPitch),
                dstW_(dstW), dstH_(dstH), dstPitch_(dstPitch) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= dstW_ || y >= dstH_) return;

                int cy = y << 1;

                auto load = [&](int yy) -> float {
                    int c = yy < 0 ? 0 : (yy >= srcH_ ? srcH_ - 1 : yy);
                    return src_[c * srcPitch_ + x];
                    };

                float a = load(cy - 2);
                float b = load(cy - 1);
                float c = load(cy);
                float d = load(cy + 1);
                float e = load(cy + 2);

                dst_[y * dstPitch_ + x] =
                    (a + 4.0f * b + 6.0f * c + 4.0f * d + e) * (1.0f / 16.0f);
            }

        private:
            const float* src_;
            float* dst_;
            int srcW_, srcH_, srcPitch_;
            int dstW_, dstH_, dstPitch_;
        };

        // ============================================================================
        // 光流上采样内核
        // ============================================================================

        class FlowUpsampleKernel {
        public:
            FlowUpsampleKernel(
                const sycl::float2* coarse, sycl::float2* fine,
                int cW, int cH, int cPitch,
                int fW, int fH, int fPitch)
                : coarse_(coarse), fine_(fine),
                cW_(cW), cH_(cH), cPitch_(cPitch),
                fW_(fW), fH_(fH), fPitch_(fPitch) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= fW_ || y >= fH_) return;

                float lx = 0.5f * (static_cast<float>(x) + 0.5f) - 0.5f;
                float ly = 0.5f * (static_cast<float>(y) + 0.5f) - 0.5f;

                int x0 = static_cast<int>(sycl::floor(lx));
                int y0 = static_cast<int>(sycl::floor(ly));
                int x1 = sycl::min(x0 + 1, cW_ - 1);
                int y1 = sycl::min(y0 + 1, cH_ - 1);
                x0 = sycl::max(0, sycl::min(x0, cW_ - 1));
                y0 = sycl::max(0, sycl::min(y0, cH_ - 1));

                float fx = lx - static_cast<float>(x0);
                float fy = ly - static_cast<float>(y0);

                sycl::float2 v00 = coarse_[y0 * cPitch_ + x0];
                sycl::float2 v10 = coarse_[y0 * cPitch_ + x1];
                sycl::float2 v01 = coarse_[y1 * cPitch_ + x0];
                sycl::float2 v11 = coarse_[y1 * cPitch_ + x1];

                float w00 = (1.0f - fx) * (1.0f - fy);
                float w10 = fx * (1.0f - fy);
                float w01 = (1.0f - fx) * fy;
                float w11 = fx * fy;

                sycl::float2 r;
                r.x() = w00 * v00.x() + w10 * v10.x()
                    + w01 * v01.x() + w11 * v11.x();
                r.y() = w00 * v00.y() + w10 * v10.y()
                    + w01 * v01.y() + w11 * v11.y();

                r.x() *= static_cast<float>(fW_) / static_cast<float>(cW_);
                r.y() *= static_cast<float>(fH_) / static_cast<float>(cH_);

                fine_[y * fPitch_ + x] = r;
            }

        private:
            const sycl::float2* coarse_;
            sycl::float2* fine_;
            int cW_, cH_, cPitch_;
            int fW_, fH_, fPitch_;
        };

        // ============================================================================
        // 中值滤波内核
        // ============================================================================

        class FlowMedianKernel {
        public:
            FlowMedianKernel(
                const sycl::float2* in, sycl::float2* out,
                int W, int H, int pitch)
                : in_(in), out_(out), W_(W), H_(H), pitch_(pitch) {
            }

            void operator()(sycl::nd_item<2> item) const {
                int x = item.get_global_id(0);
                int y = item.get_global_id(1);
                if (x >= W_ || y >= H_) return;

                float bu[9], bv[9];
                int n = 0;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = sycl::max(0, sycl::min(x + dx, W_ - 1));
                        int ny = sycl::max(0, sycl::min(y + dy, H_ - 1));
                        sycl::float2 v = in_[ny * pitch_ + nx];
                        bu[n] = v.x();
                        bv[n] = v.y();
                        ++n;
                    }
                }

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

                sycl::float2 r;
                r.x() = bu[4];
                r.y() = bv[4];
                out_[y * pitch_ + x] = r;
            }

        private:
            const sycl::float2* in_;
            sycl::float2* out_;
            int W_, H_, pitch_;
        };

        // ============================================================================
        // 主机端接口
        // ============================================================================

        extern "C" {

            bool ljSyclLaunchRbSOR(
                sycl::queue& q,
                sycl::float2* fwdFlow,
                sycl::float2* bwdFlow,
                const float* I0,
                const float* I1,
                int W, int H,
                float lambda, float omega, float gamma)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            RbSORKernel(fwdFlow, bwdFlow, I0, I1,
                                W, H, W, lambda, omega, gamma, 0));
                        });

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            RbSORKernel(fwdFlow, bwdFlow, I0, I1,
                                W, H, W, lambda, omega, gamma, 1));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchPyramidDown(
                sycl::queue& q,
                const float* src,
                float* dst,
                int srcW, int srcH,
                int dstW, int dstH,
                float* tempBuffer)
            {
                try {
                    sycl::range<2> globalH(
                        static_cast<size_t>((dstW + 15) / 16 * 16),
                        static_cast<size_t>((srcH + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(globalH, local),
                            PyramidDownHKernel(src, tempBuffer,
                                srcW, srcH, srcW,
                                dstW, srcH, dstW));
                        });

                    sycl::range<2> globalV(
                        static_cast<size_t>((dstW + 15) / 16 * 16),
                        static_cast<size_t>((dstH + 15) / 16 * 16));

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(globalV, local),
                            PyramidDownVKernel(tempBuffer, dst,
                                dstW, srcH, dstW,
                                dstW, dstH, dstW));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchFlowUpsample(
                sycl::queue& q,
                const sycl::float2* coarse,
                sycl::float2* fine,
                int cW, int cH,
                int fW, int fH)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((fW + 15) / 16 * 16),
                        static_cast<size_t>((fH + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            FlowUpsampleKernel(coarse, fine,
                                cW, cH, cW,
                                fW, fH, fW));
                        });

                    q.wait();
                    return true;
                }
                catch (const sycl::exception& e) {
                    return false;
                }
            }

            bool ljSyclLaunchFlowMedian(
                sycl::queue& q,
                const sycl::float2* in,
                sycl::float2* out,
                int W, int H)
            {
                try {
                    sycl::range<2> global(
                        static_cast<size_t>((W + 15) / 16 * 16),
                        static_cast<size_t>((H + 15) / 16 * 16));
                    sycl::range<2> local(16, 16);

                    q.submit([&](sycl::handler& h) {
                        h.parallel_for(sycl::nd_range<2>(global, local),
                            FlowMedianKernel(in, out, W, H, W));
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