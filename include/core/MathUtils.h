#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Lingjing {
    namespace Math {

        // ============================================================================
        // 常量
        // ============================================================================

        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTwoPi = 2.0f * kPi;
        constexpr float kHalfPi = 0.5f * kPi;
        constexpr float kDegToRad = kPi / 180.0f;
        constexpr float kRadToDeg = 180.0f / kPi;
        constexpr float kEpsilon = 1e-6f;

        // ============================================================================
        // 基础运算
        // ============================================================================

        template<typename T>
        inline T clamp(T value, T minVal, T maxVal) {
            return (value < minVal) ? minVal : ((value > maxVal) ? maxVal : value);
        }

        template<typename T>
        inline T saturate(T value) {
            return clamp(value, T(0), T(1));
        }

        template<typename T>
        inline T lerp(T a, T b, float t) {
            return a + (b - a) * t;
        }

        inline float smoothStep(float edge0, float edge1, float x) {
            float t = saturate((x - edge0) / (edge1 - edge0));
            return t * t * (3.0f - 2.0f * t);
        }

        inline float smootherStep(float edge0, float edge1, float x) {
            float t = saturate((x - edge0) / (edge1 - edge0));
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        // ============================================================================
        // 三角函数
        // ============================================================================

        inline float degToRad(float deg) { return deg * kDegToRad; }
        inline float radToDeg(float rad) { return rad * kRadToDeg; }

        // ============================================================================
        // 图像处理
        // ============================================================================

        // 双线性采样权重
        struct BilinearWeights {
            float w00, w10, w01, w11;
            int x0, y0, x1, y1;
        };

        inline BilinearWeights computeBilinearWeights(
            float x, float y, int width, int height)
        {
            BilinearWeights w;

            x = clamp(x, 0.0f, static_cast<float>(width - 1));
            y = clamp(y, 0.0f, static_cast<float>(height - 1));

            w.x0 = static_cast<int>(x);
            w.y0 = static_cast<int>(y);
            w.x1 = (w.x0 + 1 < width) ? w.x0 + 1 : w.x0;
            w.y1 = (w.y0 + 1 < height) ? w.y0 + 1 : w.y0;

            float fx = x - static_cast<float>(w.x0);
            float fy = y - static_cast<float>(w.y0);

            w.w00 = (1.0f - fx) * (1.0f - fy);
            w.w10 = fx * (1.0f - fy);
            w.w01 = (1.0f - fx) * fy;
            w.w11 = fx * fy;

            return w;
        }

        // Catmull-Rom 三次权重
        inline float catmullRomWeight(float x, float a = -0.75f) {
            float ax = std::fabs(x);
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

        // ============================================================================
        // 颜色空间
        // ============================================================================

        inline float srgbToLinear(float srgb) {
            if (srgb <= 0.04045f) {
                return srgb / 12.92f;
            }
            return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        }

        inline float linearToSrgb(float linear) {
            if (linear <= 0.0031308f) {
                return linear * 12.92f;
            }
            return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        }

        // ============================================================================
        // 向量运算（简单）
        // ============================================================================

        inline float dot2(float ax, float ay, float bx, float by) {
            return ax * bx + ay * by;
        }

        inline float length2(float x, float y) {
            return std::sqrt(x * x + y * y);
        }

        inline float lengthSquared2(float x, float y) {
            return x * x + y * y;
        }

        // ============================================================================
        // 统计
        // ============================================================================

        inline float mean(const float* data, size_t count) {
            if (count == 0) return 0.0f;

            float sum = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                sum += data[i];
            }
            return sum / static_cast<float>(count);
        }

        inline float variance(const float* data, size_t count) {
            if (count < 2) return 0.0f;

            float m = mean(data, count);
            float sum = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                float d = data[i] - m;
                sum += d * d;
            }
            return sum / static_cast<float>(count - 1);
        }

        inline float stdDev(const float* data, size_t count) {
            return std::sqrt(variance(data, count));
        }

        // ============================================================================
        // 数值稳定性
        // ============================================================================

        inline bool isFinite(float x) {
            return std::isfinite(x);
        }

        inline float safeDivide(float numerator, float denominator,
            float fallback = 0.0f) {
            if (std::fabs(denominator) < kEpsilon) {
                return fallback;
            }
            return numerator / denominator;
        }

        inline float fastRsqrt(float x) {
            // 快速倒数平方根（Quake III 算法的现代版本）
            if (x <= 0.0f) return 0.0f;
            return 1.0f / std::sqrt(x);
        }

        // ============================================================================
        // 位操作
        // ============================================================================

        inline uint32_t alignUp(uint32_t value, uint32_t alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        inline uint64_t alignUp64(uint64_t value, uint64_t alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        inline bool isPowerOfTwo(uint32_t x) {
            return x > 0 && (x & (x - 1)) == 0;
        }

        inline uint32_t nextPowerOfTwo(uint32_t x) {
            if (x == 0) return 1;
            x--;
            x |= x >> 1;
            x |= x >> 2;
            x |= x >> 4;
            x |= x >> 8;
            x |= x >> 16;
            return x + 1;
        }

        inline int bitCount(uint32_t x) {
            int count = 0;
            while (x) {
                x &= (x - 1);
                ++count;
            }
            return count;
        }

    } // namespace Math
} // namespace Lingjing