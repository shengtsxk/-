#pragma once

#include "core/Types.h"
#include "pipeline/D3D11SuperResolution.h"

#include <memory>
#include <functional>
#include <mutex>
#include <deque>
#include <vector>
#include <cstdint>

namespace Lingjing {

    // ============================================================================
    // 呈现方式
    // ============================================================================

    enum class PresentMode : uint32_t {
        Immediate = 0,       // 立即呈现
        VSync,               // 垂直同步
        SyncInterval,        // 指定同步间隔
        Mailbox,             // 邮箱模式（低延迟）
    };

    // ============================================================================
    // 呈现配置
    // ============================================================================

    struct PresentConfig {
        PresentMode mode = PresentMode::Immediate;
        uint32_t syncInterval = 0;
        bool allowTearing = true;
        bool enableFrameQueue = true;
        uint32_t maxQueueSize = 3;

        // 超分辨率（ED-ASR，输出前放大）
        bool superResEnabled = false;
        uint32_t superResScale = 2;    // 2/3/4/5
    };

    // ============================================================================
    // 帧队列条目
    // ============================================================================

    struct FrameQueueEntry {
        GpuTexture frame;
        TimestampNs targetTimestampNs = 0;
        uint32_t frameId = 0;
    };

    // ============================================================================
    // 呈现器
    // ============================================================================

    class Presenter {
    public:
        Presenter();
        ~Presenter();

        bool initialize(void* targetHwnd, const PresentConfig& config);

        void shutdown();

        // 提交一帧到队列
        bool submitFrame(const GpuTexture& frame,
            TimestampNs targetTimeNs);

        // 触发呈现（若到达目标时间）
        bool presentIfReady();

        // 立即呈现
        bool presentNow(const GpuTexture& frame);

        // 设置插帧倍率（影响输出中间帧数量）
        void setMultiplier(uint32_t m) { multiplier_ = m; }

        // 运行中动态切换超分辨率（开关 + 倍率）
        void setSuperResolution(bool enabled, uint32_t scale);

        // 清空队列
        void clearQueue();

        // 查询
        size_t queueSize() const;
        bool isReady() const { return initialized_; }

        // 统计
        struct Stats {
            uint64_t presentCount = 0;
            uint64_t droppedCount = 0;
            float avgPresentMs = 0.0f;
            float lastPresentMs = 0.0f;
        };

        Stats stats() const;

        // 回调
        using PresentCallback = std::function<void(uint64_t frameId)>;

        void setPresentCallback(PresentCallback cb) {
            presentCallback_ = std::move(cb);
        }

    private:
        void* targetHwnd_ = nullptr;
        PresentConfig config_;
        bool initialized_ = false;

        // ---- 真实输出（GDI + CPU 帧混合） ----
        void* hOutputWnd_ = nullptr;      // 输出预览窗口 HWND
        uint32_t outW_ = 0;               // 输出宽度
        uint32_t outH_ = 0;               // 输出高度
        uint32_t multiplier_ = 2;         // 插帧倍率
        uint32_t frameCounter_ = 0;       // 倍率循环计数
        bool havePrevBits_ = false;       // 是否已缓存上一帧

        std::vector<uint8_t> prevFrameBits_;  // 上一帧 BGRA 像素
        std::vector<uint8_t> curFrameBits_;   // 当前帧 BGRA 像素

        // ---- 超分辨率（ED-ASR） ----
        bool superResEnabled_ = false;
        uint32_t superResScale_ = 2;
        D3D11SuperResolution superRes_;       // 输出前放大（独立 D3D11 设备）
        std::vector<uint8_t> presentBits_;    // 超分后的 BGRA 像素（显示用）

        // ---- 内部辅助（Windows 实现） ----
        bool grabTargetFrame();            // 抓取目标窗口画面
        bool pullQueueFrame();             // 取管线提交的最新帧并读回 CPU 像素
        void presentInterpolated();        // 帧混合插帧并输出到窗口
        void followTargetWindow();         // 输出窗口跟随目标窗口位置/尺寸

        // ---- GPU 后端（D3D11 着色器混合插帧） ----
        bool initGpuBlend();               // 初始化 D3D11 设备/着色器
        void shutdownGpuBlend();
        bool gpuBlendFrame(const uint8_t* prev, const uint8_t* cur,
            uint8_t* out, uint32_t w, uint32_t h, float alpha);

        void* d3dDevice_ = nullptr;        // ID3D11Device*
        void* d3dContext_ = nullptr;       // ID3D11DeviceContext*
        void* texPrev_ = nullptr;          // ID3D11Texture2D*（上一帧）
        void* texCur_ = nullptr;           // ID3D11Texture2D*（当前帧）
        void* texOut_ = nullptr;           // ID3D11Texture2D*（混合结果）
        void* texStaging_ = nullptr;       // ID3D11Texture2D*（读回）
        void* srvPrev_ = nullptr;          // ID3D11ShaderResourceView*
        void* srvCur_ = nullptr;           // ID3D11ShaderResourceView*
        void* rtvOut_ = nullptr;           // ID3D11RenderTargetView*
        void* shaderPS_ = nullptr;         // ID3D11PixelShader*
        void* shaderVS_ = nullptr;         // ID3D11VertexShader*
        void* sampler_ = nullptr;          // ID3D11SamplerState*
        void* cbuffer_ = nullptr;          // ID3D11Buffer*（alpha 常量）
        bool gpuBlendReady_ = false;
        uint32_t gpuTexW_ = 0;
        uint32_t gpuTexH_ = 0;

        mutable std::mutex mutex_;
        std::deque<FrameQueueEntry> queue_;

        Stats stats_;
        uint64_t nextFrameId_ = 0;

        PresentCallback presentCallback_;
    };

} // namespace Lingjing