#include "learning/LearningDatabase.h"
#include "core/Logger.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace fs = std::filesystem;

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        LearningDatabase::LearningDatabase(const std::string& dbPath)
            : dbPath_(dbPath) {
        }

        LearningDatabase::~LearningDatabase() {
            if (dirty_) {
                save();
            }
        }

        // ============================================================================
        // 加载
        // ============================================================================

        bool LearningDatabase::load() {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!fs::exists(dbPath_)) {
                LOG_INFO("Learning database not found, starting fresh: %s",
                    dbPath_.c_str());
                return true;
            }

            std::ifstream f(dbPath_, std::ios::binary);

            if (!f.is_open()) {
                LOG_ERROR("Failed to open learning database: %s", dbPath_.c_str());
                return false;
            }

            uint32_t version = 0;
            f.read(reinterpret_cast<char*>(&version), sizeof(version));

            if (version != kFormatVersion) {
                LOG_WARN("Learning DB version mismatch (%u != %u), "
                    "ignoring old data",
                    version, kFormatVersion);
                return true;
            }

            uint32_t count = 0;
            f.read(reinterpret_cast<char*>(&count), sizeof(count));

            LOG_INFO("Loading %u profiles from learning DB", count);

            for (uint32_t i = 0; i < count; ++i) {
                GameProfile p;

                if (!deserializeProfile(f, p)) {
                    LOG_WARN("Failed to read profile %u, aborting", i);
                    break;
                }

                if (p.gameHash != 0) {
                    profiles_[p.gameHash] = std::move(p);
                }
            }

            dirty_ = false;

            LOG_INFO("Learning DB loaded: %zu profiles", profiles_.size());
            return true;
        }

        // ============================================================================
        // 保存
        // ============================================================================

        bool LearningDatabase::save() {
            std::lock_guard<std::mutex> lock(mutex_);

            return saveAs(dbPath_);
        }

        bool LearningDatabase::saveAs(const std::string& path) {
            // 创建目录
            try {
                fs::path p(path);
                fs::create_directories(p.parent_path());
            }
            catch (const std::exception& e) {
                LOG_ERROR("Failed to create DB directory: %s", e.what());
                return false;
            }

            // 原子写入（先写临时文件）
            std::string tmpPath = path + ".tmp";

            std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);

            if (!f.is_open()) {
                LOG_ERROR("Failed to open temp DB file: %s", tmpPath.c_str());
                return false;
            }

            f.write(reinterpret_cast<const char*>(&kFormatVersion),
                sizeof(kFormatVersion));

            uint32_t count = static_cast<uint32_t>(profiles_.size());
            f.write(reinterpret_cast<const char*>(&count), sizeof(count));

            for (const auto& [hash, profile] : profiles_) {
                serializeProfile(f, profile);
            }

            f.close();

            // 重命名
            try {
                if (fs::exists(path)) {
                    fs::remove(path);
                }
                fs::rename(tmpPath, path);
            }
            catch (const std::exception& e) {
                LOG_ERROR("Failed to rename temp DB: %s", e.what());
                return false;
            }

            dirty_ = false;

            LOG_INFO("Learning DB saved: %zu profiles -> %s",
                profiles_.size(), path.c_str());

            return true;
        }

        // ============================================================================
        // 查询
        // ============================================================================

        GameProfile* LearningDatabase::findProfile(uint64_t gameHash) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);

            return (it != profiles_.end()) ? &it->second : nullptr;
        }

        const GameProfile* LearningDatabase::findProfile(uint64_t gameHash) const {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);

            return (it != profiles_.end()) ? &it->second : nullptr;
        }

        // ============================================================================
        // 创建/更新
        // ============================================================================

        GameProfile& LearningDatabase::getOrCreateProfile(
            uint64_t gameHash,
            const std::string& displayName,
            const std::string& processPath)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);

            if (it != profiles_.end()) {
                return it->second;
            }

            GameProfile p;
            p.gameHash = gameHash;
            p.displayName = displayName;
            p.processPath = processPath;

            // 提取 exe 名
            size_t pos = processPath.find_last_of("\\/");
            if (pos != std::string::npos) {
                p.exeName = processPath.substr(pos + 1);
            }
            else {
                p.exeName = processPath;
            }

            p.firstSeenTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            p.lastSeenTimestamp = p.firstSeenTimestamp;

            auto result = profiles_.emplace(gameHash, std::move(p));
            dirty_ = true;

            LOG_INFO("Created new game profile: %s (hash=0x%llX)",
                displayName.c_str(),
                static_cast<unsigned long long>(gameHash));

            return result.first->second;
        }

        void LearningDatabase::recordSession(uint64_t gameHash) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            it->second.totalSessions++;
            it->second.lastSeenTimestamp = std::chrono::duration_cast<
                std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

            dirty_ = true;
        }

        void LearningDatabase::recordFrame(uint64_t gameHash) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            it->second.totalFramesProcessed++;

            // 每 1000 帧标记为脏
            if ((it->second.totalFramesProcessed % 1000) == 0) {
                dirty_ = true;
            }
        }

        // ============================================================================
        // 参数更新
        // ============================================================================

        void LearningDatabase::updateProjection(
            uint64_t gameHash,
            float fx, float fy, float cx, float cy,
            float confidence)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            auto& proj = it->second.projection;

            if (proj.samplesCollected == 0) {
                proj.fx = fx;
                proj.fy = fy;
                proj.cx = cx;
                proj.cy = cy;
                proj.samplesCollected = 1;
            }
            else {
                // Welford 在线均值
                uint32_t n = proj.samplesCollected + 1;
                float alpha = 1.0f / static_cast<float>(n);

                float dx = fx - proj.fx;
                proj.fx += alpha * dx;

                float dy = fy - proj.fy;
                proj.fy += alpha * dy;

                float dcx = cx - proj.cx;
                proj.cx += alpha * dcx;

                float dcy = cy - proj.cy;
                proj.cy += alpha * dcy;

                // 方差估计
                float varInc = dx * dx * alpha * static_cast<float>(n - 1);
                proj.variance = (proj.variance * static_cast<float>(n - 1) + varInc)
                    / static_cast<float>(n);

                proj.samplesCollected = n;
            }

            proj.confidence = confidence;

            // 收敛判定
            if (proj.samplesCollected >= 100 && proj.variance < 1.0f) {
                proj.learned = true;
            }

            dirty_ = true;
        }

        void LearningDatabase::updateDepthStats(
            uint64_t gameHash,
            const float* depth, size_t count)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            auto& stats = it->second.depthStats;

            for (size_t i = 0; i < count; ++i) {
                float d = depth[i];

                if (stats.samplesCollected == 0) {
                    stats.min = stats.max = stats.mean = d;
                    stats.samplesCollected = 1;
                    continue;
                }

                uint32_t n = stats.samplesCollected + 1;
                float alpha = 1.0f / static_cast<float>(n);

                float delta = d - stats.mean;
                stats.mean += alpha * delta;
                float delta2 = d - stats.mean;
                stats.stdDev = std::sqrt(
                    (stats.stdDev * stats.stdDev * static_cast<float>(n - 1) +
                        delta * delta2) / static_cast<float>(n));

                if (d < stats.min) stats.min = d;
                if (d > stats.max) stats.max = d;

                int bin = static_cast<int>(d * 63.0f);
                if (bin < 0) bin = 0;
                if (bin > 63) bin = 63;
                stats.histogram[bin] += 1.0f;

                stats.samplesCollected = n;
            }

            if ((stats.samplesCollected % 10000) < count) {
                dirty_ = true;
            }
        }

        void LearningDatabase::updateMotionStats(
            uint64_t gameHash,
            float flowMagnitude, float rotationRate)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            auto& stats = it->second.motion;

            const float alpha = 0.01f;

            stats.avgSpeed = stats.avgSpeed * (1.0f - alpha) + flowMagnitude * alpha;
            stats.avgRotationRate = stats.avgRotationRate * (1.0f - alpha) +
                rotationRate * alpha;

            if (flowMagnitude > stats.maxSpeed) stats.maxSpeed = flowMagnitude;
            if (rotationRate > stats.maxRotationRate) {
                stats.maxRotationRate = rotationRate;
            }
        }

        void LearningDatabase::updateTAA(
            uint64_t gameHash,
            uint32_t sequenceLength,
            float maxJitter,
            float confidence)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            auto& taa = it->second.taa;
            taa.sequenceLength = sequenceLength;
            taa.maxJitter = maxJitter;
            taa.confidence = confidence;
            taa.learned = confidence > 0.7f;

            dirty_ = true;
        }

        void LearningDatabase::updatePreferences(
            uint64_t gameHash,
            const UserPreferences& prefs)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return;

            it->second.preferences = prefs;
            dirty_ = true;
        }

        // ============================================================================
        // 统计
        // ============================================================================

        std::vector<uint64_t> LearningDatabase::recentGames(int count) const {
            std::lock_guard<std::mutex> lock(mutex_);

            std::vector<std::pair<int64_t, uint64_t>> sorted;

            for (const auto& [hash, p] : profiles_) {
                sorted.emplace_back(p.lastSeenTimestamp, hash);
            }

            std::sort(sorted.begin(), sorted.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });

            std::vector<uint64_t> result;

            for (int i = 0; i < count && i < static_cast<int>(sorted.size()); ++i) {
                result.push_back(sorted[i].second);
            }

            return result;
        }

        std::vector<uint64_t> LearningDatabase::allGameHashes() const {
            std::lock_guard<std::mutex> lock(mutex_);

            std::vector<uint64_t> result;
            result.reserve(profiles_.size());

            for (const auto& [hash, _] : profiles_) {
                result.push_back(hash);
            }

            return result;
        }

        // ============================================================================
        // 序列化
        // ============================================================================

        void LearningDatabase::serializeProfile(
            std::ofstream& f, const GameProfile& p) const
        {
            f.write(reinterpret_cast<const char*>(&p.gameHash), sizeof(p.gameHash));

            auto writeString = [&f](const std::string& s) {
                uint32_t len = static_cast<uint32_t>(s.size());
                f.write(reinterpret_cast<const char*>(&len), sizeof(len));
                f.write(s.data(), len);
                };

            writeString(p.displayName);
            writeString(p.processPath);
            writeString(p.exeName);

            f.write(reinterpret_cast<const char*>(&p.firstSeenTimestamp),
                sizeof(p.firstSeenTimestamp));
            f.write(reinterpret_cast<const char*>(&p.lastSeenTimestamp),
                sizeof(p.lastSeenTimestamp));
            f.write(reinterpret_cast<const char*>(&p.totalSessions),
                sizeof(p.totalSessions));
            f.write(reinterpret_cast<const char*>(&p.totalFramesProcessed),
                sizeof(p.totalFramesProcessed));
            f.write(reinterpret_cast<const char*>(&p.totalRuntimeHours),
                sizeof(p.totalRuntimeHours));

            f.write(reinterpret_cast<const char*>(&p.projection),
                sizeof(p.projection));
            f.write(reinterpret_cast<const char*>(&p.depthStats),
                sizeof(p.depthStats));
            f.write(reinterpret_cast<const char*>(&p.taa), sizeof(p.taa));
            f.write(reinterpret_cast<const char*>(&p.motion), sizeof(p.motion));
            f.write(reinterpret_cast<const char*>(&p.render), sizeof(p.render));
            f.write(reinterpret_cast<const char*>(&p.preferences),
                sizeof(p.preferences));

            // FOV 曲线
            uint32_t fovCount = static_cast<uint32_t>(p.fovCurve.size());
            f.write(reinterpret_cast<const char*>(&fovCount), sizeof(fovCount));

            for (const auto& e : p.fovCurve) {
                f.write(reinterpret_cast<const char*>(&e), sizeof(e));
            }
        }

        bool LearningDatabase::deserializeProfile(
            std::ifstream& f, GameProfile& p)
        {
            if (!f.read(reinterpret_cast<char*>(&p.gameHash), sizeof(p.gameHash))) {
                return false;
            }

            auto readString = [&f](std::string& s) -> bool {
                uint32_t len = 0;
                if (!f.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;

                if (len > 1024 * 1024) return false;  // 防异常

                s.resize(len);
                if (len > 0) {
                    if (!f.read(s.data(), len)) return false;
                }
                return true;
                };

            if (!readString(p.displayName)) return false;
            if (!readString(p.processPath)) return false;
            if (!readString(p.exeName)) return false;

            f.read(reinterpret_cast<char*>(&p.firstSeenTimestamp),
                sizeof(p.firstSeenTimestamp));
            f.read(reinterpret_cast<char*>(&p.lastSeenTimestamp),
                sizeof(p.lastSeenTimestamp));
            f.read(reinterpret_cast<char*>(&p.totalSessions),
                sizeof(p.totalSessions));
            f.read(reinterpret_cast<char*>(&p.totalFramesProcessed),
                sizeof(p.totalFramesProcessed));
            f.read(reinterpret_cast<char*>(&p.totalRuntimeHours),
                sizeof(p.totalRuntimeHours));

            f.read(reinterpret_cast<char*>(&p.projection), sizeof(p.projection));
            f.read(reinterpret_cast<char*>(&p.depthStats), sizeof(p.depthStats));
            f.read(reinterpret_cast<char*>(&p.taa), sizeof(p.taa));
            f.read(reinterpret_cast<char*>(&p.motion), sizeof(p.motion));
            f.read(reinterpret_cast<char*>(&p.render), sizeof(p.render));
            f.read(reinterpret_cast<char*>(&p.preferences), sizeof(p.preferences));

            uint32_t fovCount = 0;
            if (!f.read(reinterpret_cast<char*>(&fovCount), sizeof(fovCount))) {
                return false;
            }

            if (fovCount > 10000) return false;

            p.fovCurve.resize(fovCount);

            for (uint32_t i = 0; i < fovCount; ++i) {
                if (!f.read(reinterpret_cast<char*>(&p.fovCurve[i]),
                    sizeof(GameProfile::FOVEvent))) {
                    return false;
                }
            }

            return true;
        }

        // ============================================================================
        // 导入导出
        // ============================================================================

        std::vector<uint8_t> LearningDatabase::exportProfile(
            uint64_t gameHash) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            std::vector<uint8_t> result;

            auto it = profiles_.find(gameHash);
            if (it == profiles_.end()) return result;

            // 使用临时文件流
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            // 简化：直接序列化到 vector

            // 使用 ofstream 到 stringstream 会略复杂，简化实现
            // 生产环境可使用序列化库

            return result;
        }

        bool LearningDatabase::importProfile(const std::vector<uint8_t>& data) {
            if (data.empty()) return false;

            std::lock_guard<std::mutex> lock(mutex_);

            // 简化实现
            return false;
        }

        // ============================================================================
        // 清除
        // ============================================================================

        void LearningDatabase::removeProfile(uint64_t gameHash) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = profiles_.find(gameHash);
            if (it != profiles_.end()) {
                profiles_.erase(it);
                dirty_ = true;

                LOG_INFO("Removed game profile: 0x%llX",
                    static_cast<unsigned long long>(gameHash));
            }
        }

        void LearningDatabase::clearAll() {
            std::lock_guard<std::mutex> lock(mutex_);

            profiles_.clear();
            dirty_ = true;

            LOG_INFO("Learning database cleared");
        }

    } // namespace Learning
} // namespace Lingjing