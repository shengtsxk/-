#include "learning/LearningManager.h"
#include "core/Logger.h"
#include "core/PathUtils.h"

#include <thread>
#include <chrono>
#include <fstream>
#include <cstring>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        LearningManager::LearningManager() = default;

        LearningManager::~LearningManager() {
            shutdown();
        }

        // ============================================================================
        // 初始化
        // ============================================================================

        bool LearningManager::initialize(const LearningManagerConfig& config) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (initialized_) return true;

            if (!config.enabled) {
                LOG_INFO("Learning system disabled");
                config_ = config;
                initialized_ = true;
                return true;
            }

            config_ = config;

            // 创建数据库
            db_ = std::make_unique<LearningDatabase>(config.dbPath);

            if (!db_->load()) {
                LOG_WARN("Failed to load learning database, starting fresh");
            }

            // 创建子模块
            learner_ = std::make_unique<OnlineLearner>(db_.get());
            transfer_ = std::make_unique<TransferLearning>(db_.get());
            active_ = std::make_unique<ActiveLearning>(db_.get());

            // 启动自动保存线程
            if (config.autoSave) {
                autoSaveStop_ = false;

                autoSaveThread_ = std::thread(
                    [this] { autoSaveThreadProc(); });
            }

            initialized_ = true;

            LOG_INFO("LearningManager initialized: %zu games known",
                db_->totalGamesLearned());

            return true;
        }

        void LearningManager::shutdown() {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!initialized_) return;

            // 结束会话
            if (learner_ && learner_->inSession()) {
                learner_->endSession();
            }

            // 停止自动保存
            autoSaveStop_ = true;

            if (autoSaveThread_.joinable()) {
                autoSaveThread_.join();
            }

            // 保存
            if (db_) {
                db_->save();
            }

            // 清理
            active_.reset();
            transfer_.reset();
            learner_.reset();
            db_.reset();

            initialized_ = false;

            LOG_INFO("LearningManager shutdown");
        }

        // ============================================================================
        // 会话
        // ============================================================================

        bool LearningManager::beginGameSession(const GameFingerprint& fp,
            const std::string& displayName,
            const std::string& processPath)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!config_.enabled || !learner_) return false;

            // 结束旧会话
            if (learner_->inSession()) {
                learner_->endSession();
            }

            // 开始新会话
            learner_->beginSession(fp, displayName, processPath);

            LOG_INFO("Learning session: %s", displayName.c_str());

            return true;
        }

        void LearningManager::endGameSession() {
            std::lock_guard<std::mutex> lock(mutex_);

            if (learner_ && learner_->inSession()) {
                learner_->endSession();

                if (db_) {
                    db_->save();
                }
            }
        }

        // ============================================================================
        // 每帧
        // ============================================================================

        void LearningManager::onFrame(const GeometryState& state,
            const float* depth,
            const float* flow,
            int W, int H)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!config_.enabled || !learner_) return;

            learner_->onFrame(state, depth, flow, W, H);
        }

        // ============================================================================
        // 初始化提示
        // ============================================================================

        InitializationHints LearningManager::getHints() const {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!learner_) return InitializationHints{};

            return learner_->getHints();
        }

        // ============================================================================
        // 用户反馈
        // ============================================================================

        bool LearningManager::submitFeedback(const UserFeedback& fb) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!config_.enabled || !active_ || !learner_) return false;

            const GameProfile* profile = learner_->currentProfile();
            if (!profile) return false;

            return active_->processFeedback(profile->gameHash, fb);
        }

        // ============================================================================
        // 偏好
        // ============================================================================

        UserPreferences LearningManager::currentPreferences() const {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!learner_) return UserPreferences{};

            const GameProfile* profile = learner_->currentProfile();

            if (profile) {
                return profile->preferences;
            }

            return UserPreferences{};
        }

        void LearningManager::updatePreferences(const UserPreferences& prefs) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!learner_) return;

            learner_->updateUserPreferences(prefs);
        }

        // ============================================================================
        // 游戏列表
        // ============================================================================

        std::vector<LearningManager::GameEntry> LearningManager::listGames() const {
            std::lock_guard<std::mutex> lock(mutex_);

            std::vector<GameEntry> result;

            if (!db_) return result;

            auto hashes = db_->allGameHashes();

            for (uint64_t hash : hashes) {
                const GameProfile* profile = db_->findProfile(hash);
                if (!profile) continue;

                GameEntry e;
                e.hash = hash;
                e.displayName = profile->displayName;
                e.exeName = profile->exeName;
                e.totalSessions = profile->totalSessions;
                e.totalFrames = profile->totalFramesProcessed;
                e.confidence = profile->overallConfidence();
                e.lastSeenTimestamp = profile->lastSeenTimestamp;

                result.push_back(std::move(e));
            }

            // 按最后访问排序
            std::sort(result.begin(), result.end(),
                [](const GameEntry& a, const GameEntry& b) {
                    return a.lastSeenTimestamp > b.lastSeenTimestamp;
                });

            return result;
        }

        std::vector<LearningManager::GameEntry> LearningManager::recentGames(
            int count) const
        {
            auto all = listGames();

            if (count < 0 || count >= static_cast<int>(all.size())) {
                return all;
            }

            all.resize(count);
            return all;
        }

        bool LearningManager::removeGame(uint64_t hash) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!db_) return false;

            db_->removeProfile(hash);
            db_->save();

            return true;
        }

        void LearningManager::clearAllData() {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!db_) return;

            db_->clearAll();
            db_->save();

            LOG_INFO("All learning data cleared");
        }

        // ============================================================================
        // 统计
        // ============================================================================

        LearningStats LearningManager::stats() const {
            std::lock_guard<std::mutex> lock(mutex_);

            LearningStats s;

            if (!db_) return s;

            auto hashes = db_->allGameHashes();

            s.totalGames = hashes.size();

            for (uint64_t hash : hashes) {
                const GameProfile* profile = db_->findProfile(hash);
                if (!profile) continue;

                if (profile->isWellLearned()) {
                    s.wellLearnedGames++;
                }

                s.totalFramesProcessed += profile->totalFramesProcessed;
                s.totalRuntimeHours += profile->totalRuntimeHours;
            }

            // 当前会话
            if (learner_ && learner_->inSession()) {
                s.currentSessionFrames = learner_->framesThisSession();
                s.currentSessionSeconds = learner_->sessionSeconds();

                const GameProfile* profile = learner_->currentProfile();
                if (profile) {
                    s.currentGameConfidence = profile->overallConfidence();
                    s.currentGameProjectionLearned = profile->projection.learned;
                }
            }

            return s;
        }

        // ============================================================================
        // 模型导出/导入（.ljm 社区共享）
        // ============================================================================

        bool LearningManager::exportModel(
            const std::string& path,
            std::string& error,
            size_t* gameCount)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!db_) {
                error = "学习数据库未初始化";
                return false;
            }

            auto hashes = db_->allGameHashes();
            if (hashes.empty()) {
                error = "还没有可导出的学习数据（先运行一段时间让软件学习）";
                return false;
            }

            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) {
                error = "无法创建模型文件：" + path;
                return false;
            }

            // magic "LJM1"
            const char magic[4] = { 'L', 'J', 'M', '1' };
            f.write(magic, 4);

            uint32_t count = static_cast<uint32_t>(hashes.size());
            f.write(reinterpret_cast<const char*>(&count), 4);

            for (uint64_t hash : hashes) {
                auto data = db_->exportProfile(hash);
                uint32_t sz = static_cast<uint32_t>(data.size());
                f.write(reinterpret_cast<const char*>(&sz), 4);
                f.write(reinterpret_cast<const char*>(data.data()), data.size());
            }

            f.close();
            if (!f.good()) {
                error = "写入模型文件失败（磁盘空间不足？）";
                return false;
            }

            if (gameCount) *gameCount = static_cast<size_t>(count);

            LOG_INFO("Learning model exported: %s (%u games)",
                path.c_str(), count);
            return true;
        }

        bool LearningManager::importModel(
            const std::string& path,
            std::string& error,
            size_t* importedGames)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!db_) {
                error = "学习数据库未初始化";
                return false;
            }

            std::ifstream f(path, std::ios::binary);
            if (!f) {
                error = "无法打开模型文件：" + path;
                return false;
            }

            char magic[4] = {};
            f.read(magic, 4);
            if (std::memcmp(magic, "LJM1", 4) != 0) {
                error = "不是有效的灵境学习模型文件（.ljm）";
                return false;
            }

            uint32_t count = 0;
            f.read(reinterpret_cast<char*>(&count), 4);
            if (count > 100000) {
                error = "模型文件异常（游戏数量超出上限）";
                return false;
            }

            size_t imported = 0;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t sz = 0;
                f.read(reinterpret_cast<char*>(&sz), 4);
                if (sz == 0 || sz > 10 * 1024 * 1024) {
                    error = "模型文件损坏（profile 块长度异常）";
                    return false;
                }
                std::vector<uint8_t> data(sz);
                f.read(reinterpret_cast<char*>(data.data()), data.size());
                if (db_->importProfile(data)) {
                    ++imported;
                }
            }

            if (imported > 0) {
                db_->save();
            }

            if (importedGames) *importedGames = imported;

            LOG_INFO("Learning model imported: %s (%zu/%u games merged)",
                path.c_str(), imported, count);
            return true;
        }

        // ============================================================================
        // 保存
        // ============================================================================

        void LearningManager::save() {
            std::lock_guard<std::mutex> lock(mutex_);

            if (db_) {
                db_->save();
            }
        }

        bool LearningManager::inSession() const {
            std::lock_guard<std::mutex> lock(mutex_);

            return learner_ && learner_->inSession();
        }

        // ============================================================================
        // 自动保存线程
        // ============================================================================

        void LearningManager::autoSaveThreadProc() {
            LOG_INFO("Learning auto-save thread started");

            while (!autoSaveStop_.load()) {
                // 每秒检查一次
                for (uint32_t i = 0; i < config_.autoSaveIntervalSeconds; ++i) {
                    if (autoSaveStop_.load()) break;

                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                if (autoSaveStop_.load()) break;

                // 保存
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (db_ && db_->isDirty()) {
                        db_->save();
                    }
                }
            }

            LOG_INFO("Learning auto-save thread stopped");
        }

    } // namespace Learning
} // namespace Lingjing