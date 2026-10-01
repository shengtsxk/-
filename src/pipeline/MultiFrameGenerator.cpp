#include "pipeline/MultiFrameGenerator.h"
#include "pipeline/D3D11FrameInterpolator.h"
#include "core/Logger.h"
#include "core/Timer.h"

#include <cstring>

namespace Lingjing {

// ============================================================================
// 构造/析构
// ============================================================================

MultiFrameGenerator::MultiFrameGenerator() = default;

MultiFrameGenerator::~MultiFrameGenerator() {
    shutdown();
}

// ============================================================================
// 初始化
// ============================================================================

bool MultiFrameGenerator::initialize(IGpuContext* gpuContext,
    IFlowEngine* flowEngine,
    OcclusionHandler* occlusionHandler,
    FrameBlender* frameBlender,
    IAIRepair* aiRepair)
{
    if (initialized_) return true;

    gpuContext_ = gpuContext;
    flowEngine_ = flowEngine;
    occlusionHandler_ = occlusionHandler;
    frameBlender_ = frameBlender;
    aiRepair_ = aiRepair;

    // D3D11 插帧核心（免外部依赖，Windows 自带硬件加速）
    d3d11_ = std::make_unique<D3D11FrameInterpolator>();
    if (!d3d11_->initialize()) {
        LOG_WARN("MultiFrameGenerator: D3D11 interpolator unavailable, "
            "frame generation disabled");
        d3d11_.reset();
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    LOG_INFO("MultiFrameGenerator initialized (D3D11 compute interpolator)");
    return true;
}

void MultiFrameGenerator::shutdown() {
    if (d3d11_) d3d11_->shutdown();
    d3d11_.reset();

    framePool_.clear();
    prevFrames_.clear();
    hasPrevFrames_ = false;
    initialized_ = false;
}

// ============================================================================
// 生成 N 帧
// ============================================================================

bool MultiFrameGenerator::generateFrames(const GpuTexture& frame0,
    const GpuTexture& frame1,
    const GpuTexture& prevFrame,
    const MultiFrameConfig& config,
    MultiFrameResult& result)
{
    if (!initialized_ || !d3d11_) return false;

    result.reset();

    // 输入必须是 CPU 像素缓冲（当前管线 WGC 读回路径）
    if (!frame0.valid() || !frame1.valid()) return false;
    if (!frame0.isCpuBuffer || !frame1.isCpuBuffer) {
        LOG_WARN("MultiFrameGenerator: input frames are not CPU buffers, "
            "interpolation skipped");
        return false;
    }

    const uint32_t W = frame0.width;
    const uint32_t H = frame0.height;
    if (W == 0 || H == 0 || W != frame1.width || H != frame1.height) {
        return false;
    }

    const uint32_t N = config.multiplier;
    if (N == 0) return false;

    // 帧池：固定大容量（4K 上限），永不 realloc，保证 Presenter 异步读安全
    const size_t capBytes = (size_t)W * H * 4 >
        (size_t)4096 * 4096 * 4
        ? (size_t)W * H * 4 : (size_t)4096 * 4096 * 4;

    if (framePool_.size() < N) {
        size_t old = framePool_.size();
        framePool_.resize(N);
        for (size_t i = old; i < N; ++i) {
            framePool_[i].reserve(capBytes);
        }
    }
    for (uint32_t i = 0; i < N; ++i) {
        framePool_[i].resize((size_t)W * H * 4);
    }

    // 执行 D3D11 插帧
    float flowMs = 0.0f;
    float genMs = 0.0f;
    float quality = 0.0f;

    Timer totalTimer;
    totalTimer.start();

    bool ok = d3d11_->generateFrames(
        static_cast<const uint8_t*>(frame0.nativeHandle),
        static_cast<const uint8_t*>(frame1.nativeHandle),
        static_cast<int>(W), static_cast<int>(H),
        static_cast<int>(N),
        framePool_,
        flowMs,
        genMs,
        quality);

    if (!ok) {
        LOG_WARN("MultiFrameGenerator: D3D11 interpolation failed");
        return false;
    }

    // 构造结果帧
    const TimestampNs baseTs = frame0.timestampNs;

    for (uint32_t i = 0; i < N; ++i) {
        GpuTexture ft;
        ft.nativeHandle = framePool_[i].data();
        ft.isCpuBuffer = true;
        ft.width = W;
        ft.height = H;
        ft.rowPitch = W * 4;
        ft.format = TextureFormat::B8G8R8A8_UNORM;
        ft.frameIndex = frame0.frameIndex + i + 1;
        // 插值帧时间戳
        ft.timestampNs = baseTs +
            static_cast<TimestampNs>(
                static_cast<double>(frame1.timestampNs - baseTs) *
                (static_cast<double>(i + 1) / (N + 1)));

        result.frames.push_back(ft);
    }
    result.numFrames = N;
    result.flowMs = flowMs;
    result.occlusionMs = 0.0f;
    result.blendMs = genMs;
    result.aiMs = 0.0f;
    result.totalMs = static_cast<float>(totalTimer.elapsedMs());
    result.avgQuality = quality;

    // 周期性能日志（首次 + 每 15 次）
    {
        static thread_local int genLogCounter = 0;
        if (++genLogCounter == 1 || genLogCounter % 15 == 0) {
            LOG_INFO("[Interp] frames=%u size=%ux%u flow=%.2fms gen=%.2fms total=%.2fms quality=%.2f",
                N, W, H, flowMs, genMs, result.totalMs, quality);
        }
    }

    (void)prevFrame;

    return true;
}

// ============================================================================
// 重置
// ============================================================================

void MultiFrameGenerator::reset() {
    prevFrames_.clear();
    hasPrevFrames_ = false;
}

// ============================================================================
// 兼容辅助（D3D11 核心内部管理，保留接口）
// ============================================================================

bool MultiFrameGenerator::ensureMultiFrameBuffers(uint32_t W, uint32_t H,
    uint32_t N)
{
    (void)W; (void)H; (void)N;
    return d3d11_ != nullptr;
}

bool MultiFrameGenerator::runMultiFrameWarp(const GpuTexture& I0,
    const GpuTexture& I1,
    const GpuTexture& fwdFlow,
    const GpuTexture& bwdFlow,
    uint32_t numFrames,
    float alphaStart,
    float alphaStep)
{
    (void)I0; (void)I1; (void)fwdFlow; (void)bwdFlow;
    (void)numFrames; (void)alphaStart; (void)alphaStep;
    return false; // 由 D3D11 核心承担
}

bool MultiFrameGenerator::runAIRepair(const MultiFrameConfig& config,
    MultiFrameResult& result)
{
    (void)config; (void)result;
    return false; // AI 修复由 IAIRepair 承担
}

} // namespace Lingjing
