#pragma once

#include "core/Types.h"
#include "core/DeviceCaps.h"
#include "core/Error.h"
#include "gpu/IGpuContext.h"

#include <memory>
#include <functional>
#include <vector>

namespace Lingjing {

    // ============================================================================
    // 光流引擎类型
    // ============================================================================

    enum class FlowEngineType : uint32_t {
        None = 0,
        HuaiZhu,            // 淮竹几何反演（默认）
        NVIDIA_NVOFA,       // NVIDIA 硬件光流
        Intel_XeFlow,       // Intel 硬件光流
        Variational,        // 变分光流（回退）
    };

    inline const char* flowEngineTypeName(FlowEngineType type) {
        switch (type) {
        case FlowEngineType::HuaiZhu:        return "淮竹 HuaiZhu";
        case FlowEngineType::NVIDIA_NVOFA:   return "NVIDIA NVOFA";
        case FlowEngineType::Intel_XeFlow:   return "Intel Xe Flow";
        case FlowEngineType::Variational:    return "Variational";
        default: return "None";
        }
    }

    // ============================================================================
    // 光流求解配置
    // ============================================================================

    struct FlowSolveConfig {
        // 输入分辨率
        uint32_t inputWidth = 0;
        uint32_t inputHeight = 0;

        // 光流输出分辨率（支持上采样）
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;

        // 精度档位
        enum class Precision : uint32_t {
            Fast = 0,
            Balanced = 1,
            Quality = 2,
            Ultra = 3,
        };
        Precision precision = Precision::Balanced;

        // 是否计算反向光流
        bool computeBackward = true;

        // 硬件光流网格大小
        uint32_t gridSize = 4;

        // 一致性校验阈值
        float consistencyThreshold = 0.5f;

        // 淮竹特定
        struct HuaiZhuOptions {
            bool enableGeometryInversion = true;
            bool enableLearning = true;
            int warmupFrames = 30;
            float convergenceThreshold = 0.95f;
            float defaultFov = 78.0f;
            int depthStride = 2;      // 每 N 帧估计一次深度
        } huaiZhu;

        // 变分光流参数
        struct VariationalOptions {
            float lambda = 0.30f;
            float gamma = 1.50f;
            int iterCoarse = 24;
            int iterFine = 8;
            int numLevels = 5;
        } variational;

        // 超时
        float timeoutMs = 8.0f;
    };

    // ============================================================================
    // 光流结果
    // ============================================================================

    struct FlowResult {
        // 前向光流（t -> t+1）
        GpuTexture forwardFlow;
        bool hasForwardFlow = false;

        // 反向光流（t+1 -> t）
        GpuTexture backwardFlow;
        bool hasBackwardFlow = false;

        // 遮挡概率图
        GpuTexture occlusionProb;
        bool hasOcclusionProb = false;

        // 置信度图
        GpuTexture confidence;
        bool hasConfidence = false;

        // 全局运动（用于场景切换检测）
        Vec2f globalMotion;
        float globalRotation = 0.0f;

        // 性能计时
        float totalTimeMs = 0.0f;
        float geometryTimeMs = 0.0f;
        float depthTimeMs = 0.0f;
        float consistencyTimeMs = 0.0f;

        // 质量指标
        float estimatedQuality = 0.0f;   // 0~1
        float epeEstimate = 0.0f;         // 估计的端点误差

        void reset() {
            hasForwardFlow = false;
            hasBackwardFlow = false;
            hasOcclusionProb = false;
            hasConfidence = false;
            totalTimeMs = 0.0f;
            geometryTimeMs = 0.0f;
            depthTimeMs = 0.0f;
            consistencyTimeMs = 0.0f;
            estimatedQuality = 0.0f;
            epeEstimate = 0.0f;
        }
    };

    // ============================================================================
    // 光流引擎接口
    // ============================================================================

    class IFlowEngine {
    public:
        virtual ~IFlowEngine() = default;

        // ====================================================================
        // 生命周期
        // ====================================================================

        virtual bool initialize(IGpuContext* gpuContext,
            const FlowSolveConfig& config) = 0;

        virtual void shutdown() = 0;

        // ====================================================================
        // 求解
        // ====================================================================

        // 计算两帧之间的光流
        // prevFrame / currFrame：GPU 常驻纹理
        // 返回值：是否成功
        virtual bool solve(const GpuTexture& prevFrame,
            const GpuTexture& currFrame,
            FlowResult& result) = 0;

        // 重置内部状态（切换分辨率/场景时调用）
        virtual void reset() = 0;

        // ====================================================================
        // 查询
        // ====================================================================

        virtual bool isReady() const = 0;
        virtual FlowEngineType type() const = 0;
        virtual const char* engineName() const = 0;
        virtual GpuVendor vendor() const = 0;

        // 收敛状态（淮竹特有）
        virtual float convergence() const { return 1.0f; }
        virtual bool isWarmedUp() const { return true; }

        // ====================================================================
        // 学习系统集成（可选）
        // ====================================================================

        virtual void setLearningContext(void* context) {}
        virtual void* learningContext() const { return nullptr; }
    };

    // ============================================================================
    // 工厂（由 FlowEngineFactory.cpp 实现）
    // ============================================================================

    std::unique_ptr<IFlowEngine> createHuaiZhuEngine();
    std::unique_ptr<IFlowEngine> createNVOFAEngine();
    std::unique_ptr<IFlowEngine> createXeFlowEngine();
    std::unique_ptr<IFlowEngine> createVariationalEngine();

} // namespace Lingjing