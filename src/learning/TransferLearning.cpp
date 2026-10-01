#include "learning/TransferLearning.h"
#include "core/Logger.h"

#include <cmath>
#include <algorithm>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 构造
        // ============================================================================

        TransferLearning::TransferLearning(LearningDatabase* db)
            : db_(db) {
        }

        // ============================================================================
        // 建议
        // ============================================================================

        TransferSuggestion TransferLearning::suggest(
            uint64_t targetGameHash,
            const GameFingerprint& fp) const
        {
            TransferSuggestion result;

            if (!db_) return result;

            // 遍历所有已学习的游戏
            auto allHashes = db_->allGameHashes();

            float bestScore = 0.0f;
            const GameProfile* bestMatch = nullptr;

            for (uint64_t hash : allHashes) {
                if (hash == targetGameHash) continue;

                const GameProfile* profile = db_->findProfile(hash);
                if (!profile || !profile->isWellLearned()) continue;

                // 计算相似度
                float score = computeSimilarity(fp, *profile);

                if (score > bestScore) {
                    bestScore = score;
                    bestMatch = profile;
                }
            }

            // 阈值：相似度 > 0.7 才借用
            if (bestMatch && bestScore > 0.7f) {
                result.found = true;
                result.sourceGameHash = bestMatch->gameHash;
                result.similarity = bestScore;
                result.sourceGameName = bestMatch->displayName;

                result.projection = bestMatch->projection;
                result.depthStats = bestMatch->depthStats;
                result.taa = bestMatch->taa;

                LOG_INFO("Transfer suggestion: %s (similarity=%.2f) -> target",
                    bestMatch->displayName.c_str(), bestScore);
            }

            return result;
        }

        // ============================================================================
        // 应用
        // ============================================================================

        bool TransferLearning::applySuggestion(
            uint64_t targetGameHash,
            const TransferSuggestion& suggestion)
        {
            if (!db_ || !suggestion.found) return false;

            GameProfile* target = db_->findProfile(targetGameHash);
            if (!target) return false;

            // 降权应用：因为不是完全一致，将置信度降低
            float discount = suggestion.similarity * 0.7f;

            target->projection = suggestion.projection;
            target->projection.confidence *= discount;
            target->projection.variance *= 2.0f;

            target->depthStats = suggestion.depthStats;
            target->taa = suggestion.taa;
            target->taa.confidence *= discount;

            LOG_INFO("Applied transfer to game 0x%llX from 0x%llX (discount=%.2f)",
                static_cast<unsigned long long>(targetGameHash),
                static_cast<unsigned long long>(suggestion.sourceGameHash),
                discount);

            return true;
        }

        // ============================================================================
        // 相似度计算
        // ============================================================================

        float TransferLearning::computeSimilarity(
            const GameFingerprint& fp,
            const GameProfile& profile) const
        {
            float score = 0.0f;
            int count = 0;

            // 1. GPU 匹配（重要）
            {
                float gpuScore = 0.0f;

                // 从 profile 的 processPath 无法直接获得 GPU 信息
                // 简化：假设同 GPU 分数高

                gpuScore = 0.5f;
                score += gpuScore;
                ++count;
            }

            // 2. 深度分布相似度
            {
                if (profile.depthStats.samplesCollected > 100) {
                    // 归一化统计
                    float mean = profile.depthStats.mean;
                    float stdv = profile.depthStats.stdDev;

                    // 相似度基于 mean/std 的合理性
                    // 典型 mean 在 0.4~0.6，stdv 在 0.2~0.3
                    float meanScore = 1.0f - std::fabs(mean - 0.5f) * 2.0f;
                    float stdScore = 1.0f - std::fabs(stdv - 0.25f) * 2.0f;

                    meanScore = std::max(0.0f, std::min(1.0f, meanScore));
                    stdScore = std::max(0.0f, std::min(1.0f, stdScore));

                    score += (meanScore + stdScore) * 0.5f;
                    ++count;
                }
            }

            // 3. TAA 参数相似度
            {
                if (profile.taa.learned) {
                    // TAA 序列长度是最常见的 8 或 16
                    float taaScore = (profile.taa.sequenceLength == 8 ||
                        profile.taa.sequenceLength == 16) ? 0.8f : 0.5f;
                    score += taaScore;
                    ++count;
                }
            }

            // 4. 运动模式相似度
            {
                float avgSpeed = profile.motion.avgSpeed;

                // 典型 avgSpeed 在 1~10 之间
                float motionScore = 1.0f - std::min(avgSpeed / 20.0f, 1.0f);
                score += motionScore;
                ++count;
            }

            return (count > 0) ? (score / static_cast<float>(count)) : 0.0f;
        }

    } // namespace Learning
} // namespace Lingjing