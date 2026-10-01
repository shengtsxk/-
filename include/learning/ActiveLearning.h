#pragma once

#include "learning/LearningDatabase.h"

#include <string>
#include <vector>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 用户反馈
        // ============================================================================

        struct UserFeedback {
            // 整体评分 0~5
            int overallQuality = 3;

            // 具体问题
            bool hasJelly = false;         // 果冻效应
            bool hasGhosting = false;       // 鬼影
            bool hasStutter = false;        // 卡顿
            bool hasBlur = false;           // 模糊
            bool hasArtifacts = false;      // 其他伪影

            // 触发条件
            std::string triggerContext;

            // 时间戳
            int64_t timestamp = 0;
        };

        // ============================================================================
        // 主动学习
        // ============================================================================

        class ActiveLearning {
        public:
            ActiveLearning(LearningDatabase* db);

            // 处理用户反馈
            bool processFeedback(uint64_t gameHash,
                const UserFeedback& fb);

            // 重置学习偏好
            void resetPreferences(uint64_t gameHash);

            // 获取反馈历史
            const std::vector<UserFeedback>& feedbackHistory(
                uint64_t gameHash) const;

            // 反馈数量统计
            size_t feedbackCount(uint64_t gameHash) const;

        private:
            void adjustPreferences(UserPreferences& prefs,
                const UserFeedback& fb);

            LearningDatabase* db_;

            // 反馈历史（内存缓存）
            std::unordered_map<uint64_t, std::vector<UserFeedback>> history_;
        };

    } // namespace Learning
} // namespace Lingjing