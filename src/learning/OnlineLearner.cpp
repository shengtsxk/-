#include "learning/OnlineLearner.h"
#include "core/Logger.h"

#include <cmath>
#include <chrono>
#include <vector>
#include <algorithm>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        OnlineLearner::OnlineLearner(LearningDatabase* db)
            : db_(db) {
        }

        OnlineLearner::~OnlineLearner() {
            endSession();
        }

        // ============================================================================
        // 会话
        // ============================================================================

        void OnlineLearner::beginSession(const GameFingerprint& fp,
            const std::string& displayName,
            const std::string& processPath)
        {
            if (!db_) return;

            currentHash_ = fp.combinedHash;
            currentFp_ = fp;

            // 转宽字符路径为 UTF-8
            std::string pathUtf8(processPath.begin(), processPath.end());

            profile_ = &db_->getOrCreateProfile(
                fp.combinedHash, displayName, pathUtf8);

            db_->recordSession(fp.combinedHash);

            sessionStartTime_ = std::chrono::steady_clock::now();
            framesThisSession_ = 0;

            LOG_INFO("Learner session started: %s (%.0f%% learned)",
                displayName.c_str(),
                profile_->overallConfidence() * 100.0f);
        }

        void OnlineLearner::endSession() {
            if (!profile_) return;

            auto now = std::chrono::steady_clock::now();
            float sessionSec = std::chrono::duration<float>(
                now - sessionStartTime_).count();

            profile_->totalRuntimeHours += sessionSec / 3600.0f;

            if (db_) {
                db_->save();
            }

            LOG_INFO("Learner session ended: %.1f s, %llu frames",
                sessionSec,
                static_cast<unsigned long long>(framesThisSession_));

            profile_ = nullptr;
        }

        double OnlineLearner::sessionSeconds() const {
            if (!profile_) return 0.0;

            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(now - sessionStartTime_).count();
        }

        // ============================================================================
        // 每帧处理
        // ============================================================================

        void OnlineLearner::onFrame(const GeometryState& state,
            const float* depth,
            const float* flow,
            int W, int H)
        {
            if (!profile_ || !db_) return;

            ++framesThisSession_;
            db_->recordFrame(currentHash_);

            // 1. 更新投影（仅在收敛后）
            if (state.projectionConvergence > 0.9f) {
                db_->updateProjection(
                    currentHash_,
                    state.fx, state.fy,
                    state.cx, state.cy,
                    state.projectionConvergence);
            }

            // 2. 每 60 帧更新一次深度统计
            if ((framesThisSession_ % 60) == 0 && depth) {
                std::vector<float> samples;
                samples.reserve(static_cast<size_t>(W * H) / 16);

                for (int i = 0; i < W * H; i += 16) {
                    samples.push_back(depth[i]);
                }

                if (!samples.empty()) {
                    db_->updateDepthStats(currentHash_,
                        samples.data(),
                        samples.size());
                }
            }

            // 3. 每 4 帧更新一次运动统计
            if (flow && (framesThisSession_ % 4) == 0) {
                float sumMag = 0.0f;
                int count = 0;

                for (int i = 0; i < W * H; i += 64) {
                    float u = flow[i * 2 + 0];
                    float v = flow[i * 2 + 1];
                    sumMag += std::sqrt(u * u + v * v);
                    ++count;
                }

                if (count > 0) {
                    float avgMag = sumMag / static_cast<float>(count);
                    float rotRate = estimateRotationRate(flow, W, H);
                    db_->updateMotionStats(currentHash_, avgMag, rotRate);
                }
            }

            // 4. 每 300 帧更新一次 TAA
            if ((framesThisSession_ % 300) == 0) {
                estimateTAAParameters();
            }
        }

        // ============================================================================
        // 初始化提示
        // ============================================================================

        InitializationHints OnlineLearner::getHints() const {
            InitializationHints hints;

            if (!profile_) return hints;

            if (profile_->projection.learned) {
                hints.hasProjection = true;
                hints.fx = profile_->projection.fx;
                hints.fy = profile_->projection.fy;
                hints.cx = profile_->projection.cx;
                hints.cy = profile_->projection.cy;
                hints.confidence = profile_->projection.confidence;
            }

            if (profile_->depthStats.samplesCollected > 100) {
                hints.hasDepthStats = true;
                hints.depthMin = profile_->depthStats.min;
                hints.depthMax = profile_->depthStats.max;
                hints.depthMean = profile_->depthStats.mean;
            }

            if (profile_->taa.learned) {
                hints.hasTAA = true;
                hints.taaSequenceLength = profile_->taa.sequenceLength;
                hints.taaMaxJitter = profile_->taa.maxJitter;
            }

            hints.hasPreferences = true;
            hints.preferences = profile_->preferences;

            return hints;
        }

        // ============================================================================
        // 用户偏好
        // ============================================================================

        void OnlineLearner::updateUserPreferences(const UserPreferences& prefs) {
            if (!profile_ || !db_) return;

            profile_->preferences = prefs;
            db_->updatePreferences(currentHash_, prefs);
        }

        // ============================================================================
        // 旋转速率估计
        // ============================================================================

        float OnlineLearner::estimateRotationRate(const float* flow, int W, int H) {
            float cx = static_cast<float>(W) * 0.5f;
            float cy = static_cast<float>(H) * 0.5f;

            float sumX2Y2 = 0.0f;
            float sumTangent = 0.0f;

            const int step = 16;

            for (int y = step; y < H; y += step) {
                for (int x = step; x < W; x += step) {
                    float dx = static_cast<float>(x) - cx;
                    float dy = static_cast<float>(y) - cy;

                    int idx = (y * W + x) * 2;
                    float u = flow[idx + 0];
                    float v = flow[idx + 1];

                    sumX2Y2 += dx * dx + dy * dy;
                    sumTangent += dx * v - dy * u;
                }
            }

            if (sumX2Y2 < 1e-6f) return 0.0f;

            return std::fabs(sumTangent / sumX2Y2);
        }

        // ============================================================================
        // TAA 参数估计
        // ============================================================================

        void OnlineLearner::estimateTAAParameters() {
            if (!profile_ || !db_) return;

            // 简化：使用常见默认值
            static const uint32_t candidateLengths[] = { 2, 4, 8, 16, 32 };

            // 从历史帧分析抖动模式（简化使用 8）
            uint32_t bestLength = 8;
            float bestConfidence = 0.5f;

            db_->updateTAA(currentHash_, bestLength, 0.5f, bestConfidence);
        }

    } // namespace Learning
} // namespace Lingjing