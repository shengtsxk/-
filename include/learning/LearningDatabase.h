#pragma once

#include "core/Types.h"
#include "learning/GameFingerprint.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace Lingjing {
    namespace Learning {

        // ============================================================================
        // 用户偏好
        // ============================================================================

        struct UserPreferences {
            uint32_t preferredQuality = 2;       // 0=性能 1=平衡 2=画质 3=极致
            uint32_t preferredMultiplier = 2;
            bool aiRepairEnabled = true;
            bool deJellyEnabled = true;
            float deJellyStrength = 0.6f;
            float temporalWeight = 0.15f;
            bool occlusionAware = true;
            float occlusionThreshold = 0.55f;
            uint32_t latencyTarget = 3;           // LatencyTarget enum
        };

        // ============================================================================
        // 学习档案
        // ============================================================================

        struct GameProfile {
            // 标识
            uint64_t gameHash = 0;
            std::string displayName;
            std::string processPath;
            std::string exeName;
            int64_t firstSeenTimestamp = 0;
            int64_t lastSeenTimestamp = 0;

            // 学习统计
            uint32_t totalSessions = 0;
            uint64_t totalFramesProcessed = 0;
            float totalRuntimeHours = 0.0f;

            // ====================================================================
            // 学到的引擎参数
            // ====================================================================

            // 投影矩阵
            struct ProjectionLearning {
                float fx = 0.0f;
                float fy = 0.0f;
                float cx = 0.0f;
                float cy = 0.0f;
                float znear = 0.0f;
                float zfar = 0.0f;
                float confidence = 0.0f;
                uint32_t samplesCollected = 0;
                float variance = 0.0f;
                bool learned = false;
            } projection;

            // FOV 变化
            struct FOVEvent {
                float fovDegrees;
                float triggerFlowMagnitude;
                uint32_t durationFrames;
            };
            std::vector<FOVEvent> fovCurve;

            // 深度分布
            struct DepthStats {
                float min = 0.0f;
                float max = 1.0f;
                float mean = 0.5f;
                float stdDev = 0.25f;
                float histogram[64] = { 0 };
                uint32_t samplesCollected = 0;
            } depthStats;

            // TAA 抖动
            struct TAALearning {
                uint32_t sequenceLength = 0;
                uint32_t haltonBaseX = 2;
                uint32_t haltonBaseY = 3;
                float maxJitter = 0.5f;
                float confidence = 0.0f;
                bool learned = false;
            } taa;

            // 运动模式
            struct MotionStats {
                float avgSpeed = 0.0f;
                float maxSpeed = 0.0f;
                float speedStdDev = 0.0f;
                float avgRotationRate = 0.0f;
                float maxRotationRate = 0.0f;
            } motion;

            // 渲染特性
            struct RenderProfile {
                bool isHDR = false;
                float hdrPeakNits = 0.0f;
                uint32_t typicalRenderWidth = 0;
                uint32_t typicalRenderHeight = 0;
                bool usesUpscaler = false;
                std::string upscalerType;
                float upscalerRatio = 1.0f;
            } render;

            // 用户偏好
            UserPreferences preferences;

            // ====================================================================
            // 查询
            // ====================================================================

            float overallConfidence() const {
                float sum = 0.0f;
                int count = 0;

                if (projection.learned) {
                    sum += projection.confidence;
                    ++count;
                }

                if (taa.learned) {
                    sum += taa.confidence;
                    ++count;
                }

                if (depthStats.samplesCollected > 100) {
                    sum += 0.8f;
                    ++count;
                }

                return (count > 0) ? (sum / static_cast<float>(count)) : 0.0f;
            }

            bool isWellLearned() const {
                return projection.learned &&
                    projection.confidence > 0.9f &&
                    projection.samplesCollected > 1000 &&
                    totalFramesProcessed > 10000;
            }
        };

        // ============================================================================
        // 学习数据库
        // ============================================================================

        class LearningDatabase {
        public:
            LearningDatabase(const std::string& dbPath);
            ~LearningDatabase();

            // ------------------------------------------------------------------
            // 加载/保存
            // ------------------------------------------------------------------
            bool load();
            bool save();
            bool saveAs(const std::string& path);

            // ------------------------------------------------------------------
            // 查询
            // ------------------------------------------------------------------
            GameProfile* findProfile(uint64_t gameHash);
            const GameProfile* findProfile(uint64_t gameHash) const;

            // ------------------------------------------------------------------
            // 创建/更新
            // ------------------------------------------------------------------
            GameProfile& getOrCreateProfile(uint64_t gameHash,
                const std::string& displayName,
                const std::string& processPath);

            void recordSession(uint64_t gameHash);
            void recordFrame(uint64_t gameHash);

            // ------------------------------------------------------------------
            // 参数更新
            // ------------------------------------------------------------------
            void updateProjection(uint64_t gameHash,
                float fx, float fy, float cx, float cy,
                float confidence);

            void updateDepthStats(uint64_t gameHash,
                const float* depth, size_t count);

            void updateMotionStats(uint64_t gameHash,
                float flowMagnitude, float rotationRate);

            void updateTAA(uint64_t gameHash,
                uint32_t sequenceLength,
                float maxJitter,
                float confidence);

            void updatePreferences(uint64_t gameHash,
                const UserPreferences& prefs);

            // ------------------------------------------------------------------
            // 统计
            // ------------------------------------------------------------------
            size_t totalGamesLearned() const { return profiles_.size(); }
            std::vector<uint64_t> recentGames(int count) const;
            std::vector<uint64_t> allGameHashes() const;

            // ------------------------------------------------------------------
            // 导出/导入
            // ------------------------------------------------------------------
            std::vector<uint8_t> exportProfile(uint64_t gameHash) const;
            bool importProfile(const std::vector<uint8_t>& data);

            // ------------------------------------------------------------------
            // 清除
            // ------------------------------------------------------------------
            void removeProfile(uint64_t gameHash);
            void clearAll();

            // ------------------------------------------------------------------
            // 路径
            // ------------------------------------------------------------------
            const std::string& dbPath() const { return dbPath_; }
            void setDbPath(const std::string& path) { dbPath_ = path; }

            // ------------------------------------------------------------------
            // 状态
            // ------------------------------------------------------------------
            bool isDirty() const { return dirty_; }
            void markClean() { dirty_ = false; }

        private:
            void serializeProfile(std::ofstream& f, const GameProfile& p) const;
            bool deserializeProfile(std::ifstream& f, GameProfile& p);

            std::string dbPath_;
            std::unordered_map<uint64_t, GameProfile> profiles_;
            bool dirty_ = false;
            mutable std::mutex mutex_;

            static constexpr uint32_t kFormatVersion = 2;
        };

    } // namespace Learning
} // namespace Lingjing