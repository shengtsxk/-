#include "pipeline/PerformanceMonitor.h"
#include "core/Logger.h"

#include <algorithm>
#include <numeric>
#include <cmath>

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    PerformanceMonitor::PerformanceMonitor() {
        }

    PerformanceMonitor::~PerformanceMonitor() = default;

    // ============================================================================
    // 生命周期
    // ============================================================================

    void PerformanceMonitor::start() {
        std::lock_guard<std::mutex> lock(mutex_);

        running_ = true;
        startTime_ = std::chrono::steady_clock::now();

        LOG_INFO("PerformanceMonitor started");
    }

    void PerformanceMonitor::stop() {
        std::lock_guard<std::mutex> lock(mutex_);

        running_ = false;
    }

    void PerformanceMonitor::reset() {
        std::lock_guard<std::mutex> lock(mutex_);

        samples_.clear();
        summary_ = PerformanceSummary{};
        lastSample_ = PerformanceSample{};

        if (running_) {
            startTime_ = std::chrono::steady_clock::now();
        }
    }

    // ============================================================================
    // 采样
    // ============================================================================

    void PerformanceMonitor::recordSample(const PerformanceSample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);

        samples_.push_back(sample);
        if (samples_.size() > sampleWindow_) {
            samples_.pop_front();
        }

        lastSample_ = sample;

        // 更新摘要（每 30 帧一次）
        if (samples_.size() % 30 == 0) {
            updateSummary();
        }

        // 回调
        if (sampleCallback_) {
            sampleCallback_(sample);
        }
    }

    void PerformanceMonitor::recordFrame(float totalMs) {
        PerformanceSample sample;
        sample.timestampNs = nowNs();
        sample.totalMs = totalMs;

        recordSample(sample);
    }

    void PerformanceMonitor::recordStage(const char* name, float ms) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (strcmp(name, "capture") == 0) lastSample_.captureMs = ms;
        else if (strcmp(name, "flow") == 0) lastSample_.flowMs = ms;
        else if (strcmp(name, "geometry") == 0) lastSample_.geometryMs = ms;
        else if (strcmp(name, "ai") == 0) lastSample_.aiRepairMs = ms;
        else if (strcmp(name, "blend") == 0) lastSample_.blendMs = ms;
        else if (strcmp(name, "present") == 0) lastSample_.presentMs = ms;
    }

    // ============================================================================
    // 查询
    // ============================================================================

    PerformanceSample PerformanceMonitor::lastSample() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastSample_;
    }

    PerformanceSummary PerformanceMonitor::summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return summary_;
    }

    std::vector<PerformanceSample> PerformanceMonitor::recentSamples(
        size_t count) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<PerformanceSample> result;
        result.reserve(std::min(count, samples_.size()));

        size_t start = (samples_.size() > count) ? (samples_.size() - count) : 0;

        for (size_t i = start; i < samples_.size(); ++i) {
            result.push_back(samples_[i]);
        }

        return result;
    }

    float PerformanceMonitor::currentSourceFps() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastSample_.sourceFps;
    }

    float PerformanceMonitor::currentOutputFps() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastSample_.outputFps;
    }

    // ============================================================================
    // 摘要更新
    // ============================================================================

    void PerformanceMonitor::updateSummary() {
        if (samples_.empty()) return;

        summary_ = PerformanceSummary{};

        // 收集所有耗时
        std::vector<float> totalTimes;
        totalTimes.reserve(samples_.size());

        float sumCapture = 0.0f, sumFlow = 0.0f, sumGeometry = 0.0f;
        float sumAi = 0.0f, sumBlend = 0.0f, sumPresent = 0.0f;
        float sumSourceFps = 0.0f, sumOutputFps = 0.0f;
        float sumGpuUtil = 0.0f, sumGpuTemp = 0.0f;
        float sumGpuClock = 0.0f, sumVram = 0.0f;

        float maxSourceFps = 0.0f, maxOutputFps = 0.0f;
        float maxGpuTemp = 0.0f;

        for (const auto& s : samples_) {
            totalTimes.push_back(s.totalMs);

            sumCapture += s.captureMs;
            sumFlow += s.flowMs;
            sumGeometry += s.geometryMs;
            sumAi += s.aiRepairMs;
            sumBlend += s.blendMs;
            sumPresent += s.presentMs;
            sumSourceFps += s.sourceFps;
            sumOutputFps += s.outputFps;
            sumGpuUtil += s.gpuUtilization;
            sumGpuTemp += s.gpuTemperatureC;
            sumGpuClock += s.gpuClockMhz;
            sumVram += s.vramUsedMB;

            if (s.sourceFps > maxSourceFps) maxSourceFps = s.sourceFps;
            if (s.outputFps > maxOutputFps) maxOutputFps = s.outputFps;
            if (s.gpuTemperatureC > maxGpuTemp) maxGpuTemp = s.gpuTemperatureC;
        }

        float n = static_cast<float>(samples_.size());
        float invN = 1.0f / n;

        summary_.avgSourceFps = sumSourceFps * invN;
        summary_.avgOutputFps = sumOutputFps * invN;
        summary_.maxSourceFps = maxSourceFps;
        summary_.maxOutputFps = maxOutputFps;

        summary_.avgCaptureMs = sumCapture * invN;
        summary_.avgFlowMs = sumFlow * invN;
        summary_.avgGeometryMs = sumGeometry * invN;
        summary_.avgAiRepairMs = sumAi * invN;
        summary_.avgBlendMs = sumBlend * invN;
        summary_.avgPresentMs = sumPresent * invN;

        // 总耗时统计
        std::sort(totalTimes.begin(), totalTimes.end());

        summary_.avgTotalMs =
            std::accumulate(totalTimes.begin(), totalTimes.end(), 0.0f) * invN;
        summary_.minTotalMs = totalTimes.front();
        summary_.maxTotalMs = totalTimes.back();

        auto percentile = [&](float p) -> float {
            size_t idx = static_cast<size_t>(
                p * static_cast<float>(totalTimes.size() - 1));
            return totalTimes[idx];
            };

        summary_.p50TotalMs = percentile(0.50f);
        summary_.p95TotalMs = percentile(0.95f);
        summary_.p99TotalMs = percentile(0.99f);

        // GPU
        summary_.avgGpuUtilization = sumGpuUtil * invN;
        summary_.maxGpuTemperatureC = maxGpuTemp;
        summary_.avgGpuClockMhz = sumGpuClock * invN;
        summary_.avgVramUsedMB = sumVram * invN;

        // 时长
        if (running_) {
            summary_.elapsedSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime_).count();
        }
    }

    void PerformanceMonitor::setSampleWindow(size_t samples) {
        std::lock_guard<std::mutex> lock(mutex_);

        sampleWindow_ = (std::max)(size_t(30), (std::min)(samples, size_t(3000)));

        while (samples_.size() > sampleWindow_) {
            samples_.pop_front();
        }
    }

} // namespace Lingjing