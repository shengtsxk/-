#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <atomic>

namespace Lingjing {

// ============================================================================
// 高精度时钟类型
// ============================================================================

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;
using TimePoint = SteadyClock::time_point;
using Duration = std::chrono::nanoseconds;

// ============================================================================
// 时间工具函数
// ============================================================================

namespace TimeUtils {

// 当前时间戳（纳秒，相对 steady_clock）
int64_t nowNs();

// 当前墙上时间（毫秒，Unix 纪元）
int64_t wallClockMs();

// 当前墙上时间（微秒，Unix 纪元）
int64_t wallClockUs();

// 格式化时长
std::string formatDuration(double ms);
std::string formatDurationNs(int64_t ns);

// 格式化时间戳
std::string formatTimestamp(int64_t unixMs, const char* fmt = "%Y-%m-%d %H:%M:%S");

} // namespace TimeUtils

// ============================================================================
// 单次计时器
// ============================================================================

class Timer {
public:
    Timer();
    explicit Timer(const std::string& name);
    ~Timer() = default;

    // 启动/停止
    void start();
    void stop();
    void reset();

    // 查询状态
    bool isRunning() const;
    double elapsedMs() const;
    double elapsedUs() const;
    double elapsedNs() const;
    double elapsedSec() const;

    // 名称
    void setName(const std::string& name);
    const std::string& name() const;

private:
    std::string name_;
    TimePoint startTime_;
    TimePoint stopTime_;
    bool running_ = false;
    bool stopped_ = false;
};

// ============================================================================
// 计时统计
// ============================================================================

struct TimerStats {
    uint64_t count = 0;

    double totalMs = 0.0;
    double lastMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double meanMs = 0.0;

    // 分位数
    double p50Ms = 0.0;
    double p90Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;

    // 标准差
    double stddevMs = 0.0;

    void reset();
    void update(double ms);
};

// ============================================================================
// 命名计时器（分段计时 + 统计）
// ============================================================================

class NamedTimer {
public:
    static constexpr size_t kDefaultWindowSize = 256;

    NamedTimer();
    explicit NamedTimer(size_t windowSize);
    ~NamedTimer() = default;

    // 分段计时
    void start(const std::string& name);
    double stop(const std::string& name);

    // 手动记录
    void record(const std::string& name, double ms);

    // 查询
    TimerStats stats(const std::string& name) const;
    double lastMs(const std::string& name) const;
    double meanMs(const std::string& name) const;
    double p95Ms(const std::string& name) const;
    double smoothedMeanMs(const std::string& name, size_t window = 30) const;
    uint64_t count(const std::string& name) const;

    // 所有统计
    std::unordered_map<std::string, TimerStats> allStats() const;

    // 列出所有名称
    std::vector<std::string> names() const;

    // 重置
    void reset();
    void reset(const std::string& name);

    // 配置
    void setWindowSize(size_t size);
    size_t windowSize() const;

private:
    struct NamedEntry {
        std::deque<double> history;
        TimerStats stats;
        TimePoint startTime;
        bool running = false;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, NamedEntry> entries_;
    size_t windowSize_;
};

// ============================================================================
// RAII 作用域计时器
// ============================================================================

class ScopedTimer {
public:
    explicit ScopedTimer(NamedTimer& timer, const std::string& name);
    explicit ScopedTimer(std::function<void(double)> callback);
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    double elapsedMs() const;

private:
    NamedTimer* timer_ = nullptr;
    std::string name_;
    std::function<void(double)> callback_;
    TimePoint startTime_;
};

// ============================================================================
// 帧率计数器
// ============================================================================

class FpsCounter {
public:
    static constexpr size_t kDefaultWindow = 60;

    explicit FpsCounter(size_t windowSize = kDefaultWindow);
    ~FpsCounter() = default;

    // 每帧调用一次
    void tick();

    // 查询
    double fps() const;
    double frameTimeMs() const;
    double minFrameTimeMs() const;
    double maxFrameTimeMs() const;

    // 样本数
    size_t samples() const;

    // 重置
    void reset();

    // 配置
    void setWindowSize(size_t size);
    size_t windowSize() const;

private:
    mutable std::mutex mutex_;
    std::deque<double> intervals_;
    size_t windowSize_;
    TimePoint lastTick_;
    bool hasLast_ = false;
};

// ============================================================================
// 计数器（带时间窗口）
// ============================================================================

class RateCounter {
public:
    explicit RateCounter(double windowSeconds = 1.0);
    ~RateCounter() = default;

    // 记录一次事件
    void increment(int64_t count = 1);

    // 查询速率（事件/秒）
    double ratePerSecond() const;

    // 窗口内的总数
    uint64_t countInWindow() const;

    // 重置
    void reset();

private:
    struct Sample {
        TimePoint time;
        int64_t count;
    };

    mutable std::mutex mutex_;
    std::deque<Sample> samples_;
    double windowSeconds_;
    uint64_t totalInWindow_ = 0;
};

// ============================================================================
// 性能采样器（用于长期监控）
// ============================================================================

class PerformanceSampler {
public:
    struct Sample {
        int64_t timestampMs;
        double value;
    };

    explicit PerformanceSampler(size_t maxSamples = 1000);
    ~PerformanceSampler() = default;

    void add(double value);
    void add(double value, int64_t timestampMs);

    std::vector<Sample> samples() const;
    std::vector<Sample> samplesSince(int64_t sinceMs) const;
    std::vector<Sample> lastN(size_t n) const;

    // 统计
    double mean() const;
    double minValue() const;
    double maxValue() const;
    double stddev() const;

    // 清除
    void clear();

    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::deque<Sample> samples_;
    size_t maxSamples_;
};

} // namespace Lingjing