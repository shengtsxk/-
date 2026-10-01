#include "pipeline/SceneDetector.h"
#include "core/Logger.h"
#include "core/MathUtils.h"

#include <cmath>
#include <algorithm>

#ifdef LJ_NVIDIA
#include <cuda_runtime.h>

namespace Lingjing {
    namespace Cuda {

        __global__ void histogramKernel(
            const float* __restrict__ luma,
            int* __restrict__ histogram,
            int total,
            int numBins)
        {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= total) return;

            float v = luma[i];
            int bin = static_cast<int>(v * static_cast<float>(numBins));
            if (bin < 0) bin = 0;
            if (bin >= numBins) bin = numBins - 1;

            atomicAdd(&histogram[bin], 1);
        }

        __global__ void lumaStatsKernel(
            const float* __restrict__ luma,
            float* __restrict__ sum,
            float* __restrict__ sumSq,
            int total)
        {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= total) return;

            float v = luma[i];
            atomicAdd(sum, v);
            atomicAdd(sumSq, v * v);
        }

        __global__ void flowMagnitudeKernel(
            const float* __restrict__ flow,
            float* __restrict__ sumMag,
            float* __restrict__ sumMagSq,
            int total)
        {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= total) return;

            float fx = flow[i * 2 + 0];
            float fy = flow[i * 2 + 1];
            float mag = sqrtf(fx * fx + fy * fy);

            atomicAdd(sumMag, mag);
            atomicAdd(sumMagSq, mag * mag);
        }

    } // namespace Cuda
} // namespace Lingjing

#endif

namespace Lingjing {

    // ============================================================================
    // 构造/析构
    // ============================================================================

    SceneDetector::SceneDetector() = default;
    SceneDetector::~SceneDetector() { shutdown(); }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool SceneDetector::initialize(IGpuContext* gpuContext,
        const SceneDetectorConfig& config)
    {
        if (initialized_) return true;

        if (!gpuContext || !gpuContext->isValid()) {
            LOG_ERROR("SceneDetector: invalid GPU context");
            return false;
        }

        gpuContext_ = gpuContext;
        config_ = config;

        histogramHistory_.clear();

        initialized_ = true;

        LOG_INFO("SceneDetector initialized (bins=%d, cut=%.2f)",
            config_.histogramBins, config_.sceneCutThreshold);
        return true;
    }

    void SceneDetector::shutdown() {
        if (!initialized_) return;

        histogramHistory_.clear();
        gpuContext_ = nullptr;
        initialized_ = false;
    }

    void SceneDetector::reset() {
        std::lock_guard<std::mutex> lock(mutex_);

        histogramHistory_.clear();
        lastAnalysis_ = SceneAnalysis{};
        currentType_ = SceneType::Unknown;
        flashCounter_ = 0;
    }

    // ============================================================================
    // 分析
    // ============================================================================

    bool SceneDetector::analyzeFrame(const GpuTexture& currentFrame,
        const GpuTexture& prevFrame,
        SceneAnalysis& result)
    {
        if (!initialized_) return false;

        std::lock_guard<std::mutex> lock(mutex_);

        result = SceneAnalysis{};
        result.timestampNs = currentFrame.timestampNs;
        result.frameIndex = currentFrame.frameIndex;

        // 简化实现：基于 CPU 分析（实际使用 GPU 内核）
        // 完整实现使用 CUDA histogramKernel 和 lumaStatsKernel

        // 检测历史大小
        if (histogramHistory_.size() >= 1) {
            // 计算直方图差异
            std::vector<float> hist;
            computeHistogram(currentFrame, hist);

            float diff = compareHistograms(histogramHistory_.back(), hist);
            result.histogramDiff = diff;

            if (diff > config_.sceneCutThreshold) {
                result.isSceneCut = true;
            }

            histogramHistory_.push_back(hist);
            if (histogramHistory_.size() > 30) {
                histogramHistory_.pop_front();
            }
        }
        else {
            std::vector<float> hist;
            computeHistogram(currentFrame, hist);
            histogramHistory_.push_back(hist);
        }

        // 从光流分析运动
        // 注：这里假设外部已经计算了光流，通过 analyzeFromFlow 更新

        classifyScene(result);

        lastAnalysis_ = result;
        currentType_ = result.type;

        return true;
    }

    void SceneDetector::analyzeFromFlow(const GpuTexture& flowField,
        SceneAnalysis& result)
    {
        if (!initialized_ || !flowField.valid()) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // 在 GPU 上计算运动幅度统计
        // 简化实现：假设外部已经计算
        // 完整实现使用 flowMagnitudeKernel

        // 分类
        classifyScene(result);

        if (result.globalMotionMagnitude > config_.fastMotionThreshold) {
            result.type = SceneType::FastMotion;
        }
        else if (result.globalMotionMagnitude > config_.slowMotionThreshold) {
            result.type = SceneType::NormalMotion;
        }
        else if (result.globalMotionMagnitude > config_.staticThreshold) {
            result.type = SceneType::SlowMotion;
        }
        else {
            result.type = SceneType::Static;
        }

        lastAnalysis_ = result;
        currentType_ = result.type;
    }

    // ============================================================================
    // 直方图计算
    // ============================================================================

    void SceneDetector::computeHistogram(const GpuTexture& frame,
        std::vector<float>& histogram)
    {
        histogram.assign(config_.histogramBins, 0.0f);

        // 简化：使用 CPU 遍历（实际应使用 GPU 内核）
        // 完整实现调用 Cuda::histogramKernel

        // 假设 frame 是 R32_FLOAT 格式且主机可访问
        // 实际使用时需要从 GPU 下载或通过互操作

        // 占位：使用均匀分布（真实实现需要 GPU 下载）
        for (int i = 0; i < config_.histogramBins; ++i) {
            histogram[i] = 1.0f / static_cast<float>(config_.histogramBins);
        }
    }

    float SceneDetector::compareHistograms(const std::vector<float>& h1,
        const std::vector<float>& h2)
    {
        if (h1.size() != h2.size() || h1.empty()) return 0.0f;

        // Bhattacharyya 距离
        float bc = 0.0f;
        for (size_t i = 0; i < h1.size(); ++i) {
            bc += std::sqrt(h1[i] * h2[i]);
        }

        // 归一化：bc=1 完全相同，bc=0 完全不同
        // 差异 = 1 - bc
        return 1.0f - bc;
    }

    // ============================================================================
    // 场景分类
    // ============================================================================

    void SceneDetector::classifyScene(SceneAnalysis& analysis) {
        // 1. 场景切换优先级最高
        if (analysis.isSceneCut) {
            analysis.type = SceneType::SceneCut;
            analysis.confidence = analysis.histogramDiff;
            return;
        }

        // 2. 闪光
        if (analysis.isFlash) {
            analysis.type = SceneType::Flash;
            analysis.confidence = 0.9f;
            return;
        }

        // 3. 运动幅度分类
        float mag = analysis.globalMotionMagnitude;

        if (mag < config_.staticThreshold) {
            analysis.type = SceneType::Static;
        }
        else if (mag < config_.slowMotionThreshold) {
            analysis.type = SceneType::SlowMotion;
        }
        else if (mag < config_.fastMotionThreshold) {
            analysis.type = SceneType::NormalMotion;
        }
        else {
            analysis.type = SceneType::FastMotion;
        }

        // 4. 置信度
        analysis.confidence = 0.8f;
    }

} // namespace Lingjing