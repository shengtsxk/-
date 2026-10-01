#pragma once

#include "core/Types.h"
#include "core/DeviceCaps.h"

#include <deque>
#include <mutex>
#include <atomic>
#include <functional>

namespace Lingjing {

    // ============================================================================
    // 性能采样
    // ============================================================================

    struct PerformanceSample {
        // 时间戳
        TimestampNs timestampNs = 0;

        // 帧率
        float sourceFps = 0.0f;
        float outputFps = 0.0f;
        float effectiveMultiplier = 0.0f;

        // 单帧耗时
        float captureMs = 0.0f;
        float flowMs = 0.0f;
        float geometryMs = 0.0f;
        float occlusionMs = 0.0f;
        float aiRepairMs = 0.0f;
        float blendMs = 0.0f;
        float presentMs = 0.0f;
        float totalMs = 0.0f;

        // GPU 状态
        float gpuUtilization = 0.0f;
        float gpuTemperatureC = 0.0f;
        float gpuClockMhz = 0.0f;
        float vramUsedMB = 0.0f;
        float vramTotalMB = 0.0f;

        // 质量
        float estimatedQuality = 0.0f;
        uint64_t droppedFrames = 0;
    };

    // ============================================================================
    // 性能统计
    // ============================================================================

    struct PerformanceSummary {
        // 帧率
        float avgSourceFps = 0.0f;
        float avgOutputFps = 0.0f;
        float maxSourceFps = 0.0f;
        float maxOutputFps = 0.0f;

        // 耗时
        float avgTotalMs = 0.0f;
        float minTotalMs = 0.0f;
        float maxTotalMs = 0.0f;
        float p50TotalMs = 0.0f;
        float p95TotalMs = 0.0f;
        float p99TotalMs = 0.0f;

        // 分阶段平均
        float avgCaptureMs = 0.0f;
        float avgFlowMs = 0.0f;
        float avgGeometryMs = 0.0f;
        float avgAiRepairMs = 0.0f;
        float avgBlendMs = 0.0f;
        float avgPresentMs = 0.0f;

        // GPU
        float avgGpuUtilization = 0.0f;
        float maxGpuTemperatureC = 0.0f;
        float avgGpuClockMhz = 0.0f;
        float avgVramUsedMB = 0.0f;

        // 计数
        uint64_t totalFramesProcessed = 0;
        uint64_t totalFramesDropped = 0;
        uint64_t totalFramesGenerated = 0;

        // 时长
        double elapsedSeconds = 0.0;

        float droppedRate() const {
            if (totalFramesProcessed == 0) return 0.0f;
            return static_cast<float>(totalFramesDropped) /
                static_cast<float>(totalFramesProcessed);
        }
    };

    // ============================================================================
    // 性能监控器
    // ============================================================================

    class PerformanceMonitor {
    public:
        PerformanceMonitor();
        ~PerformanceMonitor();

        // ------------------------------------------------------------------
        // 生命周期
        // ------------------------------------------------------------------
        void start();
        void stop();
        void reset();

        // ------------------------------------------------------------------
        // 采样
        // ------------------------------------------------------------------
        void recordSample(const PerformanceSample& sample);

        // 便捷方法
        void recordFrame(float totalMs);
        void recordStage(const char* name, float ms);

        // ------------------------------------------------------------------
        // 查询
        // ------------------------------------------------------------------
        PerformanceSample lastSample() const;
        PerformanceSummary summary() const;

        // 最近 N 个采样
        std::vector<PerformanceSample> recentSamples(size_t count) const;

        // 当前帧率
        float currentSourceFps() const;
        float currentOutputFps() const;

        // ------------------------------------------------------------------
        // 实时回调
        // ------------------------------------------------------------------
        using SampleCallback = std::function<void(const PerformanceSample&)>;

        void setSampleCallback(SampleCallback cb) {
            sampleCallback_ = std::move(cb);
        }

        // ------------------------------------------------------------------
        // 配置
        // ------------------------------------------------------------------
        void setSampleWindow(size_t samples);

    private:
        void updateSummary();

        mutable std::mutex mutex_;
        std::deque<PerformanceSample> samples_;

        size_t sampleWindow_ = 300;

        PerformanceSummary summary_;
        PerformanceSample lastSample_;

        std::chrono::steady_clock::time_point startTime_;
        bool running_ = false;

        SampleCallback sampleCallback_;
    };

} // namespace Lingjing