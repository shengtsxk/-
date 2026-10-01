#pragma once

#include "learning/LearningDatabase.h"
#include "learning/GameFingerprint.h"
#include "flow/TimeGeometryMemory.h"

#include <memory>
#include <chrono>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 初始化提示（从学习数据提取）
        // ============================================================================

        struct InitializationHints {
            bool hasProjection = false;
            float fx = 0.0f;
            float fy = 0.0f;
            float cx = 0.0f;
            float cy = 0.0f;
            float confidence = 0.0f;

            bool hasDepthStats = false;
            float depthMin = 0.0f;
            float depthMax = 1.0f;
            float depthMean = 0.5f;

            bool hasTAA = false;
            uint32_t taaSequenceLength = 0;
            float taaMaxJitter = 0.5f;

            bool hasPreferences = false;
            UserPreferences preferences;
        };

        // ============================================================================
        // 在线学习器
        // ============================================================================

        class OnlineLearner {
        public:
            OnlineLearner(LearningDatabase* db);
            ~OnlineLearner();

            // ------------------------------------------------------------------
            // 会话
            // ------------------------------------------------------------------
            void beginSession(const GameFingerprint& fp,
                const std::string& displayName,
                const std::string& processPath);

            void endSession();

            // ------------------------------------------------------------------
            // 每帧调用
            // ------------------------------------------------------------------
            void onFrame(const GeometryState& state,
                const float* depth,
                const float* flow,
                int W, int H);

            // ------------------------------------------------------------------
            // 初始化提示
            // ------------------------------------------------------------------
            InitializationHints getHints() const;

            // ------------------------------------------------------------------
            // 用户偏好
            // ------------------------------------------------------------------
            void updateUserPreferences(const UserPreferences& prefs);

            // ------------------------------------------------------------------
            // 查询
            // ------------------------------------------------------------------
            GameProfile* currentProfile() { return profile_; }
            const GameProfile* currentProfile() const { return profile_; }

            bool inSession() const { return profile_ != nullptr; }

            uint64_t framesThisSession() const { return framesThisSession_; }
            double sessionSeconds() const;

        private:
            float estimateRotationRate(const float* flow, int W, int H);
            void estimateTAAParameters();

            LearningDatabase* db_;
            uint64_t currentHash_ = 0;
            GameFingerprint currentFp_;
            GameProfile* profile_ = nullptr;

            std::chrono::steady_clock::time_point sessionStartTime_;
            uint64_t framesThisSession_ = 0;
        };

    } // namespace Learning
} // namespace Lingjing