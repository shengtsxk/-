#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <optional>
#include <functional>

namespace Lingjing {
    class Json;

    // ============================================================================
    // 配置项类型
    // ============================================================================

    enum class ConfigValueType {
        Boolean,
        Integer,
        Float,
        String,
        Array
    };

    struct ConfigValue {
        ConfigValueType type = ConfigValueType::String;
        bool boolValue = false;
        int64_t intValue = 0;
        double floatValue = 0.0;
        std::string stringValue;

        bool asBool(bool defaultValue = false) const;
        int64_t asInt(int64_t defaultValue = 0) const;
        double asFloat(double defaultValue = 0.0) const;
        std::string asString(const std::string& defaultValue = "") const;
    };

    // ============================================================================
    // 配置管理器
    // ============================================================================

    class Config {
    public:
        static Config& instance();

        // 加载/保存
        bool load(const std::string& filePath);
        bool save();
        bool saveAs(const std::string& filePath);

        // 路径
        const std::string& filePath() const { return filePath_; }
        void setFilePath(const std::string& path) { filePath_ = path; }

        // 通用访问
        std::optional<ConfigValue> get(const std::string& key) const;
        void set(const std::string& key, const ConfigValue& value);

        // 类型化访问
        bool getBool(const std::string& key, bool defaultValue = false) const;
        int64_t getInt(const std::string& key, int64_t defaultValue = 0) const;
        double getFloat(const std::string& key, double defaultValue = 0.0) const;
        std::string getString(const std::string& key,
            const std::string& defaultValue = "") const;

        void setBool(const std::string& key, bool value);
        void setInt(const std::string& key, int64_t value);
        void setFloat(const std::string& key, double value);
        void setString(const std::string& key, const std::string& value);

        // 检查存在性
        bool has(const std::string& key) const;
        void remove(const std::string& key);
        void clear();

        // 帧生成设置
        FrameGenSettings getFrameGenSettings() const;
        void setFrameGenSettings(const FrameGenSettings& settings);

        // 枚举所有键
        std::vector<std::string> allKeys() const;

        // 监听变化
        using ChangeCallback = std::function<void(const std::string& key)>;
        void addChangeCallback(ChangeCallback cb);
        void clearChangeCallbacks();

        // 重置为默认
        void resetToDefaults();

        // ====================================================================
        // 默认配置
        // ====================================================================
        static void populateDefaults(Config& config);

    private:
        Config() = default;
        ~Config() = default;

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        void notifyChange(const std::string& key);
        void parseJsonInto(const Json& json, const std::string& prefix);

        mutable std::mutex mutex_;
        std::string filePath_;
        std::map<std::string, ConfigValue> values_;
        std::vector<ChangeCallback> changeCallbacks_;
    };

    // ============================================================================
    // 配置键常量
    // ============================================================================

    namespace ConfigKeys {

        // 通用
        constexpr const char* kVersion = "general.version";
        constexpr const char* kLanguage = "general.language";
        constexpr const char* kTheme = "general.theme";
        constexpr const char* kStartWithWindows = "general.start_with_windows";
        constexpr const char* kMinimizeToTray = "general.minimize_to_tray";
        constexpr const char* kCheckUpdates = "general.check_updates";
        constexpr const char* kUpdateUrl = "general.update_url";

        // 日志
        constexpr const char* kLogLevel = "logging.level";
        constexpr const char* kLogToConsole = "logging.console";
        constexpr const char* kLogToFile = "logging.file";
        constexpr const char* kLogMaxSizeMB = "logging.max_size_mb";

        // GPU
        constexpr const char* kPreferredGpu = "gpu.preferred";
        constexpr const char* kEnableNvidia = "gpu.enable_nvidia";
        constexpr const char* kEnableIntel = "gpu.enable_intel";
        constexpr const char* kEnableTensorCores = "gpu.enable_tensor_cores";
        constexpr const char* kEnableXmx = "gpu.enable_xmx";
        constexpr const char* kEnableHwFlow = "gpu.enable_hw_flow";

        // 帧生成
        constexpr const char* kFrameGenEnabled = "framegen.enabled";
        constexpr const char* kFrameGenMultiplier = "framegen.multiplier";
        constexpr const char* kFrameGenQuality = "framegen.quality";
        constexpr const char* kFrameGenOcclusionAware = "framegen.occlusion_aware";
        constexpr const char* kFrameGenDeJelly = "framegen.de_jelly";
        constexpr const char* kFrameGenDeJellyStrength = "framegen.de_jelly_strength";
        constexpr const char* kFrameGenAiRepair = "framegen.ai_repair";
        constexpr const char* kFrameGenTemporalSmooth = "framegen.temporal_smooth";
        constexpr const char* kFrameGenTemporalWeight = "framegen.temporal_weight";
        constexpr const char* kFrameGenAdaptiveQuality = "framegen.adaptive_quality";
        constexpr const char* kFrameGenMaxFrameTimeMs = "framegen.max_frame_time_ms";

        // 淮竹算法
        constexpr const char* kHuaiZhuEnabled = "huaizhu.enabled";
        constexpr const char* kWarmupFrames = "huaizhu.warmup_frames";
        constexpr const char* kConvergenceThreshold = "huaizhu.convergence_threshold";
        constexpr const char* kDepthStride = "huaizhu.depth_stride";
        constexpr const char* kDefaultFov = "huaizhu.default_fov";

        // 捕获
        constexpr const char* kCaptureBackend = "capture.backend";
        constexpr const char* kCaptureCursor = "capture.cursor";
        constexpr const char* kCaptureFpsLimit = "capture.fps_limit";

        // 学习系统
        constexpr const char* kLearningEnabled = "learning.enabled";
        constexpr const char* kLearningDbPath = "learning.db_path";
        constexpr const char* kLearningAutoSave = "learning.auto_save";
        constexpr const char* kLearningCrowdEnabled = "learning.crowd_enabled";

        // AI
        constexpr const char* kAiModelPath = "ai.model_path";
        constexpr const char* kAiDevice = "ai.device";
        constexpr const char* kAiPrecision = "ai.precision";
        constexpr const char* kAiCacheDir = "ai.cache_dir";

        // UI
        constexpr const char* kUiOpacity = "ui.opacity";
        constexpr const char* kUiAlwaysOnTop = "ui.always_on_top";
        constexpr const char* kUiShowPerformance = "ui.show_performance";
        constexpr const char* kUiWallpaperEnabled = "ui.wallpaper_enabled";
        constexpr const char* kUiWallpaperPath = "ui.wallpaper_path";
        constexpr const char* kUiRippleEnabled = "ui.ripple_enabled";
        constexpr const char* kUiRippleStrength = "ui.ripple_strength";

        // 热键
        constexpr const char* kHotkeyToggle = "hotkey.toggle";
        constexpr const char* kHotkeyPanel = "hotkey.panel";
        constexpr const char* kHotkeyScreenshot = "hotkey.screenshot";

    } // namespace ConfigKeys

} // namespace Lingjing