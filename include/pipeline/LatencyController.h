#pragma once

#include "core/Types.h"
#include <deque>
#include <mutex>
#include <atomic>
#include <functional>

namespace Lingjing {

    // ============================================================================
    // 延迟控制目标
    // ============================================================================

    enum class LatencyTarget : uint32_t {
        UltraLow = 0,       // 极限低延迟（1ms）
        Low = 1,       // 低延迟（2ms）
        Balanced = 2,       // 平衡（4ms）
        Quality = 3,       // 画质优先（8ms）
        Custom = 4,       // 自定义
    };

    inline const char* latencyTargetName(LatencyTarget t) {
        switch (t) {
        case LatencyTarget::UltraLow: return "Ultra Low (1ms)";
        case LatencyTarget::Low:      return "Low (2ms)";
        case LatencyTarget::Balanced: return "Balanced (4ms)";
        case LatencyTarget::Quality:  return "Quality (8ms)";
        case LatencyTarget::Custom:   return "Custom";
        default: return "Unknown";
        }
    }

    // ============================================================================
    // 质量档位
    // ============================================================================

    enum class DynamicQuality : uint32_t {
        Minimal = 0,
        Low = 1,
        Medium = 2,
        High = 3,
        Max = 4,
    };

    // ============================================================================
    // 延迟统计
    // ============================================================================

    struct LatencyStats {
        double targetMs = 8.0;
        double currentMs = 0.0;
        double avgMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        double p50Ms = 0.0;
        double p95Ms = 0.0;
        double p99Ms = 0.0;

        uint64_t totalFrames = 0;
        uint64_t missedDeadline = 0;

        DynamicQuality currentQuality = DynamicQuality::High;

        double headroomMs() const {
            return targetMs - avgMs;
        }

        bool isSatisfied() const {
            return avgMs <= targetMs;
        }

        void reset() {
            currentMs = 0.0;
            avgMs = 0.0;
            minMs = 0.0;
            maxMs = 0.0;
            p50Ms = 0.0;
            p95Ms = 0.0;
            p99Ms = 0.0;
            totalFrames = 0;
            missedDeadline = 0;
        }
    };

    // ============================================================================
    // 延迟控制器
    // ============================================================================

    class LatencyController {
    public:
        using QualityChangeCallback =
            std::function<void(DynamicQuality oldQ, DynamicQuality newQ)>;

        LatencyController();
        ~LatencyController();

        // ------------------------------------------------------------------
        // 配置
        // ------------------------------------------------------------------
        void setTarget(LatencyTarget target);
        void setTargetMs(double ms);
        double targetMs() const { return targetMs_; }

        void setSampleWindow(size_t samples);
        void setAutoAdjust(bool enabled) { autoAdjust_ = enabled; }

        // ------------------------------------------------------------------
        // 反馈
        // ------------------------------------------------------------------
        void recordFrame(double totalMs);
        void recordStages(double captureMs,
            double flowMs,
            double geometryMs,
            double aiMs,
            double blendMs,
            double presentMs,
            double totalMs);

        void reset();

        // ------------------------------------------------------------------
        // 查询
        // ------------------------------------------------------------------
        LatencyStats stats() const;

        DynamicQuality currentQuality() const {
            return currentQuality_.load();
        }

        // 建议的下一个质量档位
        DynamicQuality suggestedQuality() const;

        // 是否应该降档
        bool shouldDownshift() const;

        // 是否应该升档
        bool shouldUpshift() const;

        // ------------------------------------------------------------------
        // 回调
        // ------------------------------------------------------------------
        void setQualityChangeCallback(QualityChangeCallback cb) {
            qualityCallback_ = std::move(cb);
        }

    private:
        void updateQuality();

        // 目标
        double targetMs_ = 8.0;
        LatencyTarget targetType_ = LatencyTarget::Quality;

        // 采样
        mutable std::mutex mutex_;
        std::deque<double> history_;

        size_t sampleWindow_ = 120;

        // 统计
        LatencyStats stats_;

        // 质量档位
        std::atomic<DynamicQuality> currentQuality_{ DynamicQuality::High };
        uint32_t stableFrames_ = 0;

        // 分阶段耗时（最近一帧）
        double lastCaptureMs_ = 0.0;
        double lastFlowMs_ = 0.0;
        double lastGeometryMs_ = 0.0;
        double lastAiMs_ = 0.0;
        double lastBlendMs_ = 0.0;
        double lastPresentMs_ = 0.0;

        bool autoAdjust_ = true;

        QualityChangeCallback qualityCallback_;
    };

} // namespace Lingjing