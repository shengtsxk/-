#pragma once

#include "learning/LearningDatabase.h"

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 迁移建议
        // ============================================================================

        struct TransferSuggestion {
            bool found = false;
            uint64_t sourceGameHash = 0;
            float similarity = 0.0f;
            std::string sourceGameName;

            GameProfile::ProjectionLearning projection;
            GameProfile::DepthStats depthStats;
            GameProfile::TAALearning taa;
        };

        // ============================================================================
        // 迁移学习
        // ============================================================================

        class TransferLearning {
        public:
            TransferLearning(LearningDatabase* db);

            // 对冷启动游戏，找相似游戏借用参数
            TransferSuggestion suggest(uint64_t targetGameHash,
                const GameFingerprint& fp) const;

            // 应用迁移建议到目标档案
            bool applySuggestion(uint64_t targetGameHash,
                const TransferSuggestion& suggestion);

        private:
            float computeSimilarity(const GameFingerprint& fp,
                const GameProfile& profile) const;

            LearningDatabase* db_;
        };

    } // namespace Learning
} // namespace Lingjing