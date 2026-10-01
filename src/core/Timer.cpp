// ============================================================================
// src/core/Timer.cpp
// 灵境 Lingjing — 计时工具完整实现
// ============================================================================

#include "core/Timer.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <numeric>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Lingjing {

// ============================================================================
// 时间工具函数
// ============================================================================

namespace TimeUtils {

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        SteadyClock::now().time_since_epoch()).count();
}

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        SystemClock::now().time_since_epoch()).count();
}

int64_t wallClockUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        SystemClock::now().time_since_epoch()).count();
}

std::string formatDuration(double ms) {
    std::ostringstream oss;

    if (ms < 0.0) {
        oss << "0.000 ms";
    } else if (ms < 1.0) {
        oss << std::fixed << std::setprecision(3) << ms << " ms";
    } else if (ms < 1000.0) {
        oss << std::fixed << std::setprecision(2) << ms << " ms";
    } else if (ms < 60000.0) {
        oss << std::fixed << std::setprecision(3) << (ms / 1000.0) << " s";
    } else {
        double sec = ms / 1000.0;
        int min = static_cast<int>(sec / 60.0);
        sec -= min * 60.0;
        oss << min << "m " << std::fixed << std::setprecision(1) << sec << "s";
    }

    return oss.str();
}

std::string formatDurationNs(int64_t ns) {
    return formatDuration(static_cast<double>(ns) / 1e6);
}

std::string formatTimestamp(int64_t unixMs, const char* fmt) {
    std::time_t t = static_cast<std::time_t>(unixMs / 1000);
    std::tm tmBuf = {};

#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif

    char buf[128] = {0};
    std::strftime(buf, sizeof(buf), fmt, &tmBuf);

    return std::string(buf);
}

} // namespace TimeUtils

// ============================================================================
// Timer 实现
// ============================================================================

Timer::Timer()
    : startTime_(SteadyClock::now()) {
}

Timer::Timer(const std::string& name)
    : name_(name)
    , startTime_(SteadyClock::now()) {
}

void Timer::start() {
    startTime_ = SteadyClock::now();
    running_ = true;
    stopped_ = false;
}

void Timer::stop() {
    if (!running_) return;

    stopTime_ = SteadyClock::now();
    running_ = false;
    stopped_ = true;
}

void Timer::reset() {
    startTime_ = SteadyClock::now();
    stopTime_ = startTime_;
    running_ = false;
    stopped_ = false;
}

bool Timer::isRunning() const {
    return running_;
}

double Timer::elapsedMs() const {
    TimePoint end = stopped_ ? stopTime_ : SteadyClock::now();
    return std::chrono::duration<double, std::milli>(end - startTime_).count();
}

double Timer::elapsedUs() const {
    TimePoint end = stopped_ ? stopTime_ : SteadyClock::now();
    return std::chrono::duration<double, std::micro>(end - startTime_).count();
}

double Timer::elapsedNs() const {
    TimePoint end = stopped_ ? stopTime_ : SteadyClock::now();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - startTime_).count());
}

double Timer::elapsedSec() const {
    return elapsedMs() / 1000.0;
}

void Timer::setName(const std::string& name) {
    name_ = name;
}

const std::string& Timer::name() const {
    return name_;
}

// ============================================================================
// TimerStats 实现
// ============================================================================

void TimerStats::reset() {
    count = 0;
    totalMs = 0.0;
    lastMs = 0.0;
    minMs = 0.0;
    maxMs = 0.0;
    meanMs = 0.0;
    p50Ms = 0.0;
    p90Ms = 0.0;
    p95Ms = 0.0;
    p99Ms = 0.0;
    stddevMs = 0.0;
}

void TimerStats::update(double ms) {
    ++count;

    lastMs = ms;
    totalMs += ms;
    meanMs = totalMs / static_cast<double>(count);

    if (count == 1) {
        minMs = ms;
        maxMs = ms;
    } else {
        if (ms < minMs) minMs = ms;
        if (ms > maxMs) maxMs = ms;
    }
}

// ============================================================================
// NamedTimer 实现
// ============================================================================

NamedTimer::NamedTimer()
    : windowSize_(kDefaultWindowSize) {
}

NamedTimer::NamedTimer(size_t windowSize)
    : windowSize_(windowSize > 0 ? windowSize : kDefaultWindowSize) {
}

void NamedTimer::start(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& entry = entries_[name];
    entry.startTime = SteadyClock::now();
    entry.running = true;
}

double NamedTimer::stop(const std::string& name) {
    auto stopTime = SteadyClock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(name);
    if (it == entries_.end() || !it->second.running) {
        return -1.0;
    }

    auto& entry = it->second;
    double ms = std::chrono::duration<double, std::milli>(
        stopTime - entry.startTime).count();

    entry.running = false;
    entry.stats.update(ms);

    entry.history.push_back(ms);
    while (entry.history.size() > windowSize_) {
        entry.history.pop_front();
    }

    // 更新分位数
    if (!entry.history.empty()) {
        std::vector<double> sorted(entry.history.begin(),
                                    entry.history.end());
        std::sort(sorted.begin(), sorted.end());

        size_t n = sorted.size();

        auto percentile = [&](double p) -> double {
            if (n == 0) return 0.0;
            double idx = p * static_cast<double>(n - 1);
            size_t lo = static_cast<size_t>(idx);
            size_t hi = (std::min)(lo + 1, n - 1);
            double frac = idx - static_cast<double>(lo);
            return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
        };

        entry.stats.p50Ms = percentile(0.50);
        entry.stats.p90Ms = percentile(0.90);
        entry.stats.p95Ms = percentile(0.95);
        entry.stats.p99Ms = percentile(0.99);

        // 标准差
        double mean = entry.stats.meanMs;
        double varSum = 0.0;

        for (double v : sorted) {
            double d = v - mean;
            varSum += d * d;
        }

        entry.stats.stddevMs = std::sqrt(varSum / static_cast<double>(n));
    }

    return ms;
}

void NamedTimer::record(const std::string& name, double ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& entry = entries_[name];
    entry.stats.update(ms);

    entry.history.push_back(ms);
    while (entry.history.size() > windowSize_) {
        entry.history.pop_front();
    }

    // 更新分位数
    if (!entry.history.empty()) {
        std::vector<double> sorted(entry.history.begin(),
                                    entry.history.end());
        std::sort(sorted.begin(), sorted.end());

        size_t n = sorted.size();

        auto percentile = [&](double p) -> double {
            if (n == 0) return 0.0;
            double idx = p * static_cast<double>(n - 1);
            size_t lo = static_cast<size_t>(idx);
            size_t hi = (std::min)(lo + 1, n - 1);
            double frac = idx - static_cast<double>(lo);
            return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
        };

        entry.stats.p50Ms = percentile(0.50);
        entry.stats.p90Ms = percentile(0.90);
        entry.stats.p95Ms = percentile(0.95);
        entry.stats.p99Ms = percentile(0.99);

        double mean = entry.stats.meanMs;
        double varSum = 0.0;

        for (double v : sorted) {
            double d = v - mean;
            varSum += d * d;
        }

        entry.stats.stddevMs = std::sqrt(varSum / static_cast<double>(n));
    }
}

TimerStats NamedTimer::stats(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(name);
    if (it == entries_.end()) return TimerStats{};
    return it->second.stats;
}

double NamedTimer::lastMs(const std::string& name) const {
    return stats(name).lastMs;
}

double NamedTimer::meanMs(const std::string& name) const {
    return stats(name).meanMs;
}

double NamedTimer::p95Ms(const std::string& name) const {
    return stats(name).p95Ms;
}

double NamedTimer::smoothedMeanMs(const std::string& name,
                                    size_t window) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(name);
    if (it == entries_.end() || it->second.history.empty()) return 0.0;

    const auto& hist = it->second.history;
    size_t start = hist.size() > window ? hist.size() - window : 0;
    size_t count = hist.size() - start;

    double sum = 0.0;
    for (size_t i = start; i < hist.size(); ++i) {
        sum += hist[i];
    }

    return sum / static_cast<double>(count);
}

uint64_t NamedTimer::count(const std::string& name) const {
    return stats(name).count;
}

std::unordered_map<std::string, TimerStats> NamedTimer::allStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<std::string, TimerStats> result;
    result.reserve(entries_.size());

    for (const auto& [name, entry] : entries_) {
        result[name] = entry.stats;
    }

    return result;
}

std::vector<std::string> NamedTimer::names() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(entries_.size());

    for (const auto& [name, _] : entries_) {
        result.push_back(name);
    }

    return result;
}

void NamedTimer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [_, entry] : entries_) {
        entry.history.clear();
        entry.stats.reset();
        entry.running = false;
    }
}

void NamedTimer::reset(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(name);
    if (it == entries_.end()) return;

    it->second.history.clear();
    it->second.stats.reset();
    it->second.running = false;
}

void NamedTimer::setWindowSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (size == 0) size = kDefaultWindowSize;

    windowSize_ = size;

    for (auto& [_, entry] : entries_) {
        while (entry.history.size() > windowSize_) {
            entry.history.pop_front();
        }
    }
}

size_t NamedTimer::windowSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return windowSize_;
}

// ============================================================================
// ScopedTimer 实现
// ============================================================================

ScopedTimer::ScopedTimer(NamedTimer& timer, const std::string& name)
    : timer_(&timer)
    , name_(name)
    , startTime_(SteadyClock::now()) {
}

ScopedTimer::ScopedTimer(std::function<void(double)> callback)
    : callback_(std::move(callback))
    , startTime_(SteadyClock::now()) {
}

ScopedTimer::~ScopedTimer() {
    double ms = elapsedMs();

    if (timer_) {
        timer_->record(name_, ms);
    }

    if (callback_) {
        try {
            callback_(ms);
        } catch (...) {
            // 忽略回调异常
        }
    }
}

double ScopedTimer::elapsedMs() const {
    return std::chrono::duration<double, std::milli>(
        SteadyClock::now() - startTime_).count();
}

// ============================================================================
// FpsCounter 实现
// ============================================================================

FpsCounter::FpsCounter(size_t windowSize)
    : windowSize_(windowSize > 0 ? windowSize : kDefaultWindow) {
}

void FpsCounter::tick() {
    TimePoint now = SteadyClock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    if (hasLast_) {
        double ms = std::chrono::duration<double, std::milli>(
            now - lastTick_).count();

        intervals_.push_back(ms);

        while (intervals_.size() > windowSize_) {
            intervals_.pop_front();
        }
    }

    lastTick_ = now;
    hasLast_ = true;
}

double FpsCounter::fps() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (intervals_.empty()) return 0.0;

    double sum = std::accumulate(intervals_.begin(),
                                   intervals_.end(), 0.0);
    double avg = sum / static_cast<double>(intervals_.size());

    return avg > 0.0 ? 1000.0 / avg : 0.0;
}

double FpsCounter::frameTimeMs() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (intervals_.empty()) return 0.0;

    double sum = std::accumulate(intervals_.begin(),
                                   intervals_.end(), 0.0);

    return sum / static_cast<double>(intervals_.size());
}

double FpsCounter::minFrameTimeMs() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (intervals_.empty()) return 0.0;

    return *std::min_element(intervals_.begin(), intervals_.end());
}

double FpsCounter::maxFrameTimeMs() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (intervals_.empty()) return 0.0;

    return *std::max_element(intervals_.begin(), intervals_.end());
}

size_t FpsCounter::samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return intervals_.size();
}

void FpsCounter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    intervals_.clear();
    lastTick_ = TimePoint{};
    hasLast_ = false;
}

void FpsCounter::setWindowSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (size == 0) size = kDefaultWindow;

    windowSize_ = size;

    while (intervals_.size() > windowSize_) {
        intervals_.pop_front();
    }
}

size_t FpsCounter::windowSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return windowSize_;
}

// ============================================================================
// RateCounter 实现
// ============================================================================

RateCounter::RateCounter(double windowSeconds)
    : windowSeconds_(windowSeconds > 0.0 ? windowSeconds : 1.0) {
}

void RateCounter::increment(int64_t count) {
    TimePoint now = SteadyClock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    samples_.push_back({now, count});
    totalInWindow_ += static_cast<uint64_t>(count);

    // 移除窗口外的样本
    while (!samples_.empty()) {
        double age = std::chrono::duration<double>(
            now - samples_.front().time).count();

        if (age > windowSeconds_) {
            totalInWindow_ -= static_cast<uint64_t>(samples_.front().count);
            samples_.pop_front();
        } else {
            break;
        }
    }
}

double RateCounter::ratePerSecond() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (samples_.empty()) return 0.0;

    TimePoint now = SteadyClock::now();
    double age = std::chrono::duration<double>(
        now - samples_.front().time).count();

    if (age < 0.001) {
        // 样本太新，用窗口大小估算
        age = windowSeconds_;
    }

    return static_cast<double>(totalInWindow_) / age;
}

uint64_t RateCounter::countInWindow() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalInWindow_;
}

void RateCounter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    samples_.clear();
    totalInWindow_ = 0;
}

// ============================================================================
// PerformanceSampler 实现
// ============================================================================

PerformanceSampler::PerformanceSampler(size_t maxSamples)
    : maxSamples_(maxSamples > 0 ? maxSamples : 1000) {
}

void PerformanceSampler::add(double value) {
    add(value, TimeUtils::wallClockMs());
}

void PerformanceSampler::add(double value, int64_t timestampMs) {
    std::lock_guard<std::mutex> lock(mutex_);

    samples_.push_back({timestampMs, value});

    while (samples_.size() > maxSamples_) {
        samples_.pop_front();
    }
}

std::vector<PerformanceSampler::Sample> PerformanceSampler::samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<Sample>(samples_.begin(), samples_.end());
}

std::vector<PerformanceSampler::Sample> PerformanceSampler::samplesSince(
    int64_t sinceMs) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Sample> result;

    for (const auto& s : samples_) {
        if (s.timestampMs >= sinceMs) {
            result.push_back(s);
        }
    }

    return result;
}

std::vector<PerformanceSampler::Sample> PerformanceSampler::lastN(
    size_t n) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Sample> result;

    size_t start = samples_.size() > n ? samples_.size() - n : 0;

    for (size_t i = start; i < samples_.size(); ++i) {
        result.push_back(samples_[i]);
    }

    return result;
}

double PerformanceSampler::mean() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (samples_.empty()) return 0.0;

    double sum = 0.0;
    for (const auto& s : samples_) {
        sum += s.value;
    }

    return sum / static_cast<double>(samples_.size());
}

double PerformanceSampler::minValue() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (samples_.empty()) return 0.0;

    double m = samples_.front().value;
    for (const auto& s : samples_) {
        if (s.value < m) m = s.value;
    }

    return m;
}

double PerformanceSampler::maxValue() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (samples_.empty()) return 0.0;

    double m = samples_.front().value;
    for (const auto& s : samples_) {
        if (s.value > m) m = s.value;
    }

    return m;
}

double PerformanceSampler::stddev() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (samples_.size() < 2) return 0.0;

    double sum = 0.0;
    for (const auto& s : samples_) {
        sum += s.value;
    }
    double mean = sum / static_cast<double>(samples_.size());

    double varSum = 0.0;
    for (const auto& s : samples_) {
        double d = s.value - mean;
        varSum += d * d;
    }

    return std::sqrt(varSum / static_cast<double>(samples_.size()));
}

void PerformanceSampler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
}

size_t PerformanceSampler::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.size();
}

} // namespace Lingjing