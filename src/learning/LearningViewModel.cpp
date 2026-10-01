#include "learning/LearningViewModel.h"
#include "core/Logger.h"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        LearningViewModel::LearningViewModel() = default;
        LearningViewModel::~LearningViewModel() = default;

        // ============================================================================
        // 绑定
        // ============================================================================

        void LearningViewModel::attach(LearningManager* manager) {
            manager_ = manager;
        }

        // ============================================================================
        // 游戏列表
        // ============================================================================

        std::vector<LearningViewModel::GameItem> LearningViewModel::gameList() const {
            std::vector<GameItem> result;

            if (!manager_) return result;

            auto entries = manager_->listGames();

            result.reserve(entries.size());

            for (const auto& e : entries) {
                GameItem item;
                item.hash = e.hash;
                item.displayName = e.displayName;
                item.exeName = e.exeName;
                item.lastSeenString = formatTimestamp(e.lastSeenTimestamp);
                item.totalSessions = e.totalSessions;
                item.totalFrames = e.totalFrames;
                item.confidencePercent = e.confidence * 100.0f;
                item.wellLearned = (e.confidence > 0.9f);

                if (item.wellLearned) {
                    item.statusText = "已完全学习";
                }
                else if (item.confidencePercent > 50.0f) {
                    item.statusText = "学习中";
                }
                else {
                    item.statusText = "未学习";
                }

                result.push_back(std::move(item));
            }

            return result;
        }

        // ============================================================================
        // 当前会话
        // ============================================================================

        LearningViewModel::SessionInfo LearningViewModel::currentSession() const {
            SessionInfo info;

            if (!manager_) return info;

            auto stats = manager_->stats();

            info.active = manager_->inSession();
            info.framesProcessed = stats.currentSessionFrames;
            info.seconds = stats.currentSessionSeconds;
            info.confidencePercent = stats.currentGameConfidence * 100.0f;
            info.projectionLearned = stats.currentGameProjectionLearned;

            if (info.active) {
                // 从当前 profile 获取游戏名
                auto learner = manager_->database();
                // 简化实现

                std::ostringstream oss;
                oss << "已处理 " << info.framesProcessed << " 帧 ("
                    << std::fixed << std::setprecision(1) << info.seconds << " s)";
                info.statusText = oss.str();
            }
            else {
                info.statusText = "未运行";
            }

            return info;
        }

        // ============================================================================
        // 统计
        // ============================================================================

        LearningViewModel::SummaryStats LearningViewModel::summaryStats() const {
            SummaryStats s;

            if (!manager_) return s;

            auto stats = manager_->stats();

            s.totalGames = stats.totalGames;
            s.wellLearnedGames = stats.wellLearnedGames;
            s.totalFrames = stats.totalFramesProcessed;
            s.totalRuntimeString = formatRuntime(stats.totalRuntimeHours);

            return s;
        }

        // ============================================================================
        // 操作
        // ============================================================================

        bool LearningViewModel::removeGame(uint64_t hash) {
            if (!manager_) return false;

            bool ok = manager_->removeGame(hash);

            if (ok) {
                notifyDataChanged();
            }

            return ok;
        }

        void LearningViewModel::clearAll() {
            if (!manager_) return;

            manager_->clearAllData();
            notifyDataChanged();
        }

        // ============================================================================
        // 通知
        // ============================================================================

        void LearningViewModel::notifyDataChanged() {
            if (changedCallback_) {
                changedCallback_();
            }
        }

        // ============================================================================
        // 格式化
        // ============================================================================

        std::string LearningViewModel::formatTimestamp(int64_t ts) const {
            if (ts <= 0) return "从未";

            std::time_t t = static_cast<std::time_t>(ts);
            std::tm tmBuf;

#ifdef _WIN32
            localtime_s(&tmBuf, &t);
#else
            localtime_r(&t, &tmBuf);
#endif

            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmBuf);

            return buf;
        }

        std::string LearningViewModel::formatRuntime(float hours) const {
            std::ostringstream oss;

            if (hours >= 1.0f) {
                oss << std::fixed << std::setprecision(1) << hours << " 小时";
            }
            else {
                oss << std::fixed << std::setprecision(0) << (hours * 60.0f) << " 分钟";
            }

            return oss.str();
        }

    } // namespace Learning
} // namespace Lingjing