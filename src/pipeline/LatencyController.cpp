#include "pipeline/LatencyController.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    LatencyController::LatencyController() {
        }

    LatencyController::~LatencyController() = default;

    // ============================================================================
    // 配置
    // ============================================================================

    void LatencyController::setTarget(LatencyTarget target) {
        std::lock_guard<std::mutex> lock(mutex_);

        targetType_ = target;

        switch (target) {
        case LatencyTarget::UltraLow: targetMs_ = 1.0; break;
        case LatencyTarget::Low:      targetMs_ = 2.0; break;
        case LatencyTarget::Balanced: targetMs_ = 4.0; break;
        case LatencyTarget::Quality:  targetMs_ = 8.0; break;
        default: break;
        }

        stats_.targetMs = targetMs_;

        LOG_INFO("Latency target: %s (%.1f ms)",
            latencyTargetName(target), targetMs_);
    }

    void LatencyController::setTargetMs(double ms) {
        std::lock_guard<std::mutex> lock(mutex_);

        targetMs_ = (std::max)(0.5, (std::min)(ms, 100.0));
        targetType_ = LatencyTarget::Custom;
        stats_.targetMs = targetMs_;

        LOG_INFO("Latency target set to %.2f ms", targetMs_);
    }

    void LatencyController::setSampleWindow(size_t samples) {
        std::lock_guard<std::mutex> lock(mutex_);

        sampleWindow_ = (std::max)(size_t(10), (std::min)(samples, size_t(1000)));

        while (history_.size() > sampleWindow_) {
            history_.pop_front();
        }
    }

    // ============================================================================
    // 反馈
    // ============================================================================

    void LatencyController::recordFrame(double totalMs) {
        std::lock_guard<std::mutex> lock(mutex_);

        history_.push_back(totalMs);
        if (history_.size() > sampleWindow_) {
            history_.pop_front();
        }

        stats_.currentMs = totalMs;
        stats_.totalFrames++;

        if (totalMs > targetMs_) {
            stats_.missedDeadline++;
        }

        // 计算统计
        if (history_.empty()) return;

        std::vector<double> sorted(history_.begin(), history_.end());
        std::sort(sorted.begin(), sorted.end());

        double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        stats_.avgMs = sum / static_cast<double>(sorted.size());
        stats_.minMs = sorted.front();
        stats_.maxMs = sorted.back();

        auto percentile = [&](double p) -> double {
            if (sorted.empty()) return 0.0;
            size_t idx = static_cast<size_t>(
                p * static_cast<double>(sorted.size() - 1));
            return sorted[idx];
            };

        stats_.p50Ms = percentile(0.50);
        stats_.p95Ms = percentile(0.95);
        stats_.p99Ms = percentile(0.99);

        // 自动调档
        if (autoAdjust_) {
            updateQuality();
        }
    }

    void LatencyController::recordStages(double captureMs,
        double flowMs,
        double geometryMs,
        double aiMs,
        double blendMs,
        double presentMs,
        double totalMs)
    {
        lastCaptureMs_ = captureMs;
        lastFlowMs_ = flowMs;
        lastGeometryMs_ = geometryMs;
        lastAiMs_ = aiMs;
        lastBlendMs_ = blendMs;
        lastPresentMs_ = presentMs;

        recordFrame(totalMs);
    }

    void LatencyController::reset() {
        std::lock_guard<std::mutex> lock(mutex_);

        history_.clear();
        stats_.reset();
        stats_.targetMs = targetMs_;

        currentQuality_ = DynamicQuality::High;
        stableFrames_ = 0;
    }

    // ============================================================================
    // 查询
    // ============================================================================

    LatencyStats LatencyController::stats() const {
        std::lock_guard<std::mutex> lock(mutex_);

        LatencyStats copy = stats_;
        copy.currentQuality = currentQuality_.load();
        return copy;
    }

    DynamicQuality LatencyController::suggestedQuality() const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (history_.empty()) return DynamicQuality::High;

        double avg = stats_.avgMs;
        double p95 = stats_.p95Ms;

        // 若 P95 超过目标 1.5 倍，降档
        if (p95 > targetMs_ * 1.5 || avg > targetMs_ * 1.2) {
            return DynamicQuality::Low;
        }

        // 若 P95 超过目标 1.1 倍，轻度降档
        if (p95 > targetMs_ * 1.1 || avg > targetMs_) {
            return DynamicQuality::Medium;
        }

        // 若平均远低于目标（< 60%），升档
        if (avg < targetMs_ * 0.6 && p95 < targetMs_ * 0.85) {
            return DynamicQuality::Max;
        }

        // 若平均低于目标（< 80%），轻度升档
        if (avg < targetMs_ * 0.8 && p95 < targetMs_ * 0.95) {
            return DynamicQuality::High;
        }

        return currentQuality_.load();
    }

    bool LatencyController::shouldDownshift() const {
        DynamicQuality suggested = suggestedQuality();
        return suggested < currentQuality_.load();
    }

    bool LatencyController::shouldUpshift() const {
        DynamicQuality suggested = suggestedQuality();
        return suggested > currentQuality_.load();
    }

    // ============================================================================
    // 质量更新
    // ============================================================================

    void LatencyController::updateQuality() {
        // 调用方（recordFrame）已持有 mutex_，此处不再调用加锁的查询方法（避免递归死锁）
        if (history_.size() < 30) return;  // 至少需要 30 帧样本

        double avg = stats_.avgMs;
        double p95 = stats_.p95Ms;
        DynamicQuality current = currentQuality_.load();
        DynamicQuality suggested = current;

        if (p95 > targetMs_ * 1.5 || avg > targetMs_ * 1.2) {
            suggested = DynamicQuality::Low;
        }
        else if (p95 > targetMs_ * 1.1 || avg > targetMs_) {
            suggested = DynamicQuality::Medium;
        }
        else if (avg < targetMs_ * 0.6 && p95 < targetMs_ * 0.85) {
            suggested = DynamicQuality::Max;
        }
        else if (avg < targetMs_ * 0.8 && p95 < targetMs_ * 0.95) {
            suggested = DynamicQuality::High;
        }

        if (suggested == current) {
            stableFrames_++;
            return;
        }

        // 防止抖动：连续 N 帧一致才切换
        static constexpr uint32_t kSwitchThreshold = 15;

        stableFrames_++;

        if (stableFrames_ < kSwitchThreshold) {
            return;
        }

        DynamicQuality oldQ = current;
        currentQuality_ = suggested;
        stableFrames_ = 0;

        LOG_INFO("Dynamic quality: %u -> %u (avg=%.2fms, p95=%.2fms, target=%.2fms)",
            static_cast<uint32_t>(oldQ),
            static_cast<uint32_t>(suggested),
            stats_.avgMs, stats_.p95Ms, targetMs_);

        if (qualityCallback_) {
            qualityCallback_(oldQ, suggested);
        }
    }

} // namespace Lingjing