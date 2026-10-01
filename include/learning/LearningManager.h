#pragma once

#include "learning/LearningDatabase.h"
#include "learning/OnlineLearner.h"
#include "learning/TransferLearning.h"
#include "learning/ActiveLearning.h"
#include "learning/GameFingerprint.h"
#include "flow/TimeGeometryMemory.h"

#include <memory>
#include <mutex>
#include <vector>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 学习管理器配置
        // ============================================================================

        struct LearningManagerConfig {
            std::string dbPath;
            bool enabled = true;
            bool autoSave = true;
            uint32_t autoSaveIntervalSeconds = 300;
            bool enableTransfer = true;
            bool enableActive = true;
        };

        // ============================================================================
        // 学习统计
        // ============================================================================

        struct LearningStats {
            size_t totalGames = 0;
            size_t wellLearnedGames = 0;
            uint64_t totalFramesProcessed = 0;
            float totalRuntimeHours = 0.0f;

            uint64_t currentSessionFrames = 0;
            double currentSessionSeconds = 0.0;

            float currentGameConfidence = 0.0f;
            bool currentGameProjectionLearned = false;
        };

        // ============================================================================
        // 学习管理器
        // ============================================================================

        class LearningManager {
        public:
            LearningManager();
            ~LearningManager();

            // ------------------------------------------------------------------
            // 生命周期
            // ------------------------------------------------------------------
            bool initialize(const LearningManagerConfig& config);
            void shutdown();

            // ------------------------------------------------------------------
            // 会话
            // ------------------------------------------------------------------
            bool beginGameSession(const GameFingerprint& fp,
                const std::string& displayName,
                const std::string& processPath);

            void endGameSession();

            // ------------------------------------------------------------------
            // 每帧
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
            // 用户反馈
            // ------------------------------------------------------------------
            bool submitFeedback(const UserFeedback& fb);

            // ------------------------------------------------------------------
            // 偏好
            // ------------------------------------------------------------------
            UserPreferences currentPreferences() const;
            void updatePreferences(const UserPreferences& prefs);

            // ------------------------------------------------------------------
            // 游戏列表
            // ------------------------------------------------------------------
            struct GameEntry {
                uint64_t hash;
                std::string displayName;
                std::string exeName;
                uint32_t totalSessions;
                uint64_t totalFrames;
                float confidence;
                int64_t lastSeenTimestamp;
            };

            std::vector<GameEntry> listGames() const;
            std::vector<GameEntry> recentGames(int count) const;

            bool removeGame(uint64_t hash);
            void clearAllData();

            // ------------------------------------------------------------------
            // 统计
            // ------------------------------------------------------------------
            LearningStats stats() const;

            // ------------------------------------------------------------------
            // 模型导出/导入（.ljm 社区共享格式，轻量统计学习模型）
            // ------------------------------------------------------------------
            bool exportModel(const std::string& path, std::string& error,
                size_t* gameCount = nullptr);
            bool importModel(const std::string& path, std::string& error,
                size_t* importedGames = nullptr);

            // ------------------------------------------------------------------
            // 保存
            // ------------------------------------------------------------------
            void save();

            // ------------------------------------------------------------------
            // 查询
            // ------------------------------------------------------------------
            bool isEnabled() const { return config_.enabled; }
            bool inSession() const;

            LearningDatabase* database() { return db_.get(); }
            const LearningDatabase* database() const { return db_.get(); }

        private:
            void autoSaveThreadProc();

            LearningManagerConfig config_;
            bool initialized_ = false;

            std::unique_ptr<LearningDatabase> db_;
            std::unique_ptr<OnlineLearner> learner_;
            std::unique_ptr<TransferLearning> transfer_;
            std::unique_ptr<ActiveLearning> active_;

            std::thread autoSaveThread_;
            std::atomic<bool> autoSaveStop_{ false };

            mutable std::mutex mutex_;
        };

    } // namespace Learning
} // namespace Lingjing