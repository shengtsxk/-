#include "learning/ActiveLearning.h"
#include "core/Logger.h"

#include <algorithm>
#include <chrono>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 构造
        // ============================================================================

        ActiveLearning::ActiveLearning(LearningDatabase* db)
            : db_(db) {
        }

        // ============================================================================
        // 处理反馈
        // ============================================================================

        bool ActiveLearning::processFeedback(uint64_t gameHash,
            const UserFeedback& fb)
        {
            if (!db_) return false;

            GameProfile* profile = db_->findProfile(gameHash);
            if (!profile) return false;

            // 保存反馈历史
            UserFeedback copy = fb;
            if (copy.timestamp == 0) {
                copy.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            }
            history_[gameHash].push_back(copy);

            // 限制历史大小
            if (history_[gameHash].size() > 100) {
                history_[gameHash].erase(history_[gameHash].begin());
            }

            // 调整偏好
            adjustPreferences(profile->preferences, fb);

            // 保存到数据库
            db_->updatePreferences(gameHash, profile->preferences);

            LOG_INFO("User feedback processed for game 0x%llX: "
                "quality=%d jelly=%d ghosting=%d stutter=%d blur=%d",
                static_cast<unsigned long long>(gameHash),
                fb.overallQuality,
                fb.hasJelly, fb.hasGhosting,
                fb.hasStutter, fb.hasBlur);

            return true;
        }

        // ============================================================================
        // 调整偏好
        // ============================================================================

        void ActiveLearning::adjustPreferences(UserPreferences& prefs,
            const UserFeedback& fb)
        {
            // 1. 果冻 → 增加去果冻强度
            if (fb.hasJelly) {
                prefs.deJellyStrength = std::min(1.0f, prefs.deJellyStrength + 0.1f);
                prefs.deJellyEnabled = true;
            }

            // 2. 鬼影 → 增加时域平滑
            if (fb.hasGhosting) {
                prefs.temporalWeight = std::min(0.4f, prefs.temporalWeight + 0.05f);
                prefs.occlusionAware = true;
            }

            // 3. 卡顿 → 降低质量档位
            if (fb.hasStutter) {
                if (prefs.preferredQuality > 0) {
                    prefs.preferredQuality--;
                }

                // 降低延迟目标
                if (prefs.latencyTarget > 0) {
                    prefs.latencyTarget--;
                }
            }

            // 4. 模糊 → 提高质量档位
            if (fb.hasBlur) {
                if (prefs.preferredQuality < 3) {
                    prefs.preferredQuality++;
                }
            }

            // 5. 伪影 → 启用 AI 修复
            if (fb.hasArtifacts) {
                prefs.aiRepairEnabled = true;
            }

            // 6. 综合评分调整
            if (fb.overallQuality <= 2) {
                // 用户不满意 → 提高画质
                if (prefs.preferredQuality < 3) {
                    prefs.preferredQuality++;
                }
                prefs.aiRepairEnabled = true;
            }
            else if (fb.overallQuality >= 4) {
                // 用户满意 → 保持
                // 若画质档位很高，可适当降低以提升性能
                if (prefs.preferredQuality == 3 && !fb.hasBlur) {
                    prefs.preferredQuality = 2;
                }
            }
        }

        // ============================================================================
        // 重置
        // ============================================================================

        void ActiveLearning::resetPreferences(uint64_t gameHash) {
            if (!db_) return;

            GameProfile* profile = db_->findProfile(gameHash);
            if (!profile) return;

            // 重置为默认
            profile->preferences = UserPreferences{};

            db_->updatePreferences(gameHash, profile->preferences);

            // 清除反馈历史
            history_.erase(gameHash);

            LOG_INFO("Preferences reset for game 0x%llX",
                static_cast<unsigned long long>(gameHash));
        }

        // ============================================================================
        // 反馈查询
        // ============================================================================

        const std::vector<UserFeedback>& ActiveLearning::feedbackHistory(
            uint64_t gameHash) const
        {
            static const std::vector<UserFeedback> empty;

            auto it = history_.find(gameHash);
            return (it != history_.end()) ? it->second : empty;
        }

        size_t ActiveLearning::feedbackCount(uint64_t gameHash) const {
            auto it = history_.find(gameHash);
            return (it != history_.end()) ? it->second.size() : 0;
        }

    } // namespace Learning
} // namespace Lingjing