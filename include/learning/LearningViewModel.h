#pragma once

#include "learning/LearningManager.h"

#include <string>
#include <vector>
#include <functional>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 学习 UI 视图模型
        // ============================================================================

        class LearningViewModel {
        public:
            LearningViewModel();
            ~LearningViewModel();

            // ------------------------------------------------------------------
            // 绑定
            // ------------------------------------------------------------------
            void attach(LearningManager* manager);

            // ------------------------------------------------------------------
            // 游戏列表
            // ------------------------------------------------------------------

            struct GameItem {
                uint64_t hash;
                std::string displayName;
                std::string exeName;
                std::string lastSeenString;
                uint32_t totalSessions;
                uint64_t totalFrames;
                float confidencePercent;
                bool wellLearned;
                std::string statusText;
            };

            std::vector<GameItem> gameList() const;

            // ------------------------------------------------------------------
            // 当前会话
            // ------------------------------------------------------------------

            struct SessionInfo {
                bool active = false;
                std::string gameName;
                uint64_t framesProcessed = 0;
                double seconds = 0.0;
                float confidencePercent = 0.0f;
                bool projectionLearned = false;
                std::string statusText;
            };

            SessionInfo currentSession() const;

            // ------------------------------------------------------------------
            // 统计
            // ------------------------------------------------------------------

            struct SummaryStats {
                size_t totalGames = 0;
                size_t wellLearnedGames = 0;
                uint64_t totalFrames = 0;
                std::string totalRuntimeString;
            };

            SummaryStats summaryStats() const;

            // ------------------------------------------------------------------
            // 操作
            // ------------------------------------------------------------------

            bool removeGame(uint64_t hash);
            void clearAll();

            // ------------------------------------------------------------------
            // 回调（数据变化通知）
            // ------------------------------------------------------------------

            using DataChangedCallback = std::function<void()>;

            void setDataChangedCallback(DataChangedCallback cb) {
                changedCallback_ = std::move(cb);
            }

            void notifyDataChanged();

        private:
            std::string formatTimestamp(int64_t ts) const;
            std::string formatRuntime(float hours) const;

            LearningManager* manager_ = nullptr;
            DataChangedCallback changedCallback_;
        };

    } // namespace Learning
} // namespace Lingjing