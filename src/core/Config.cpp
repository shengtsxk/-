#include "core/Config.h"
#include "core/Logger.h"
#include "core/Json.h"

#include <fstream>
#include <sstream>
#include <filesystem>

namespace Lingjing {

    // ============================================================================
    // ConfigValue 实现
    // ============================================================================

    bool ConfigValue::asBool(bool defaultValue) const {
        if (type != ConfigValueType::Boolean) return defaultValue;
        return boolValue;
    }

    int64_t ConfigValue::asInt(int64_t defaultValue) const {
        if (type != ConfigValueType::Integer) return defaultValue;
        return intValue;
    }

    double ConfigValue::asFloat(double defaultValue) const {
        if (type == ConfigValueType::Float) return floatValue;
        if (type == ConfigValueType::Integer) {
            return static_cast<double>(intValue);
        }
        return defaultValue;
    }

    std::string ConfigValue::asString(const std::string& defaultValue) const {
        if (type == ConfigValueType::String) return stringValue;
        return defaultValue;
    }

    // ============================================================================
    // Config 单例
    // ============================================================================

    Config& Config::instance() {
        static Config inst;
        return inst;
    }

    // ============================================================================
    // 加载
    // ============================================================================

    bool Config::load(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);

        filePath_ = filePath;

        if (!std::filesystem::exists(filePath)) {
            LOG_INFO("Config file not found, using defaults: %s",
                filePath.c_str());
            populateDefaults(*this);
            return save();
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: %s", filePath.c_str());
            populateDefaults(*this);
            return false;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        file.close();

        Json json;
        if (!json.parse(ss.str())) {
            LOG_ERROR("Failed to parse config file: %s", filePath.c_str());
            populateDefaults(*this);
            return false;
        }

        // 解析 JSON 到 values_
        values_.clear();
        parseJsonInto(json, "");

        LOG_INFO("Config loaded from %s (%zu keys)",
            filePath.c_str(), values_.size());

        // 合并默认值（用于新版本新增的配置项）
        Config defaults;
        populateDefaults(defaults);
        for (const auto& [key, value] : defaults.values_) {
            if (values_.find(key) == values_.end()) {
                values_[key] = value;
            }
        }

        return true;
    }

    void Config::parseJsonInto(const Json& json, const std::string& prefix) {
        if (json.isObject()) {
            for (const auto& [key, value] : json.objects()) {
                std::string fullKey = prefix.empty() ? key : (prefix + "." + key);
                parseJsonInto(value, fullKey);
            }
        }
        else if (json.isBool()) {
            setBool(prefix, json.asBool());
        }
        else if (json.isInt()) {
            setInt(prefix, json.asInt());
        }
        else if (json.isFloat()) {
            setFloat(prefix, json.asFloat());
        }
        else if (json.isString()) {
            setString(prefix, json.asString());
        }
    }

    // ============================================================================
    // 保存
    // ============================================================================

    bool Config::save() {
        return saveAs(filePath_);
    }

    bool Config::saveAs(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 构建 JSON 树
        Json root;
        for (const auto& [key, value] : values_) {
            Json jv;
            switch (value.type) {
            case ConfigValueType::Boolean: jv = Json(value.boolValue); break;
            case ConfigValueType::Integer: jv = Json(value.intValue); break;
            case ConfigValueType::Float:   jv = Json(value.floatValue); break;
            case ConfigValueType::String:  jv = Json(value.stringValue); break;
            default: break;
            }
            root.setNested(key, jv);
        }
        std::string jsonStr = root.toString(2);

        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file for writing: %s",
                filePath.c_str());
            return false;
        }

        file << jsonStr;
        file.close();

        LOG_INFO("Config saved to %s (%zu keys)", filePath.c_str(), values_.size());

        return true;
    }

    // ============================================================================
    // 访问
    // ============================================================================

    std::optional<ConfigValue> Config::get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        return it->second;
    }

    void Config::set(const std::string& key, const ConfigValue& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            values_[key] = value;
        }
        notifyChange(key);
    }

    bool Config::getBool(const std::string& key, bool defaultValue) const {
        auto v = get(key);
        return v ? v->asBool(defaultValue) : defaultValue;
    }

    int64_t Config::getInt(const std::string& key, int64_t defaultValue) const {
        auto v = get(key);
        return v ? v->asInt(defaultValue) : defaultValue;
    }

    double Config::getFloat(const std::string& key, double defaultValue) const {
        auto v = get(key);
        return v ? v->asFloat(defaultValue) : defaultValue;
    }

    std::string Config::getString(const std::string& key,
        const std::string& defaultValue) const
    {
        auto v = get(key);
        return v ? v->asString(defaultValue) : defaultValue;
    }

    void Config::setBool(const std::string& key, bool value) {
        ConfigValue v;
        v.type = ConfigValueType::Boolean;
        v.boolValue = value;
        set(key, v);
    }

    void Config::setInt(const std::string& key, int64_t value) {
        ConfigValue v;
        v.type = ConfigValueType::Integer;
        v.intValue = value;
        set(key, v);
    }

    void Config::setFloat(const std::string& key, double value) {
        ConfigValue v;
        v.type = ConfigValueType::Float;
        v.floatValue = value;
        set(key, v);
    }

    void Config::setString(const std::string& key, const std::string& value) {
        ConfigValue v;
        v.type = ConfigValueType::String;
        v.stringValue = value;
        set(key, v);
    }

    bool Config::has(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.find(key) != values_.end();
    }

    void Config::remove(const std::string& key) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            values_.erase(key);
        }
        notifyChange(key);
    }

    void Config::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.clear();
    }

    std::vector<std::string> Config::allKeys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> keys;
        keys.reserve(values_.size());
        for (const auto& [key, _] : values_) {
            keys.push_back(key);
        }
        return keys;
    }

    // ============================================================================
    // 帧生成设置
    // ============================================================================

    FrameGenSettings Config::getFrameGenSettings() const {
        FrameGenSettings settings;

        settings.enabled = getBool(ConfigKeys::kFrameGenEnabled, true);
        settings.multiplier = static_cast<uint32_t>(
            getInt(ConfigKeys::kFrameGenMultiplier, 2));

        int quality = static_cast<int>(
            getInt(ConfigKeys::kFrameGenQuality, 1));
        quality = (std::max)(0, (std::min)(quality, 3));
        settings.quality = static_cast<QualityLevel>(quality);

        settings.occlusionAware =
            getBool(ConfigKeys::kFrameGenOcclusionAware, true);

        settings.deJellyEnabled =
            getBool(ConfigKeys::kFrameGenDeJelly, true);
        settings.deJellyStrength = static_cast<float>(
            getFloat(ConfigKeys::kFrameGenDeJellyStrength, 0.6));

        settings.aiRepairEnabled =
            getBool(ConfigKeys::kFrameGenAiRepair, true);
        settings.aiRepairModelPath =
            getString(ConfigKeys::kAiModelPath, "");

        settings.temporalSmoothing =
            getBool(ConfigKeys::kFrameGenTemporalSmooth, true);
        settings.temporalWeight = static_cast<float>(
            getFloat(ConfigKeys::kFrameGenTemporalWeight, 0.15));

        settings.adaptiveQuality =
            getBool(ConfigKeys::kFrameGenAdaptiveQuality, true);
        settings.maxFrameTimeMs = static_cast<float>(
            getFloat(ConfigKeys::kFrameGenMaxFrameTimeMs, 8.0));

        settings.huaiZhuEnabled =
            getBool(ConfigKeys::kHuaiZhuEnabled, true);
        settings.huaiZhuWarmupFrames = static_cast<int>(
            getInt(ConfigKeys::kWarmupFrames, 30));
        settings.huaiZhuConvergence = static_cast<float>(
            getFloat(ConfigKeys::kConvergenceThreshold, 0.95));

        settings.learningEnabled =
            getBool(ConfigKeys::kLearningEnabled, true);

        return settings;
    }

    void Config::setFrameGenSettings(const FrameGenSettings& settings) {
        setBool(ConfigKeys::kFrameGenEnabled, settings.enabled);
        setInt(ConfigKeys::kFrameGenMultiplier, settings.multiplier);
        setInt(ConfigKeys::kFrameGenQuality,
            static_cast<int>(settings.quality));
        setBool(ConfigKeys::kFrameGenOcclusionAware, settings.occlusionAware);
        setBool(ConfigKeys::kFrameGenDeJelly, settings.deJellyEnabled);
        setFloat(ConfigKeys::kFrameGenDeJellyStrength, settings.deJellyStrength);
        setBool(ConfigKeys::kFrameGenAiRepair, settings.aiRepairEnabled);
        setString(ConfigKeys::kAiModelPath, settings.aiRepairModelPath);
        setBool(ConfigKeys::kFrameGenTemporalSmooth, settings.temporalSmoothing);
        setFloat(ConfigKeys::kFrameGenTemporalWeight, settings.temporalWeight);
        setBool(ConfigKeys::kFrameGenAdaptiveQuality, settings.adaptiveQuality);
        setFloat(ConfigKeys::kFrameGenMaxFrameTimeMs, settings.maxFrameTimeMs);
        setBool(ConfigKeys::kHuaiZhuEnabled, settings.huaiZhuEnabled);
        setInt(ConfigKeys::kWarmupFrames, settings.huaiZhuWarmupFrames);
        setFloat(ConfigKeys::kConvergenceThreshold, settings.huaiZhuConvergence);
        setBool(ConfigKeys::kLearningEnabled, settings.learningEnabled);
    }

    // ============================================================================
    // 回调
    // ============================================================================

    void Config::addChangeCallback(ChangeCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        changeCallbacks_.push_back(std::move(cb));
    }

    void Config::clearChangeCallbacks() {
        std::lock_guard<std::mutex> lock(mutex_);
        changeCallbacks_.clear();
    }

    void Config::notifyChange(const std::string& key) {
        std::vector<ChangeCallback> cbs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cbs = changeCallbacks_;
        }

        for (const auto& cb : cbs) {
            try {
                cb(key);
            }
            catch (...) {
                // 忽略回调异常
            }
        }
    }

    // ============================================================================
    // 默认配置
    // ============================================================================

    void Config::populateDefaults(Config& config) {
        // 通用
        config.setString(ConfigKeys::kVersion, "1.0.0");
        config.setString(ConfigKeys::kLanguage, "zh-CN");
        config.setString(ConfigKeys::kTheme, "dark");
        config.setBool(ConfigKeys::kStartWithWindows, false);
        config.setBool(ConfigKeys::kMinimizeToTray, true);
        config.setBool(ConfigKeys::kCheckUpdates, true);
        // 更新清单地址（留空则不检查；可填本地文件或线上 URL）
        config.setString(ConfigKeys::kUpdateUrl, "file:///C:/Users/Asgard/AppData/Roaming/LingjingProject/Lingjing/version.json");

        // 日志
        config.setString(ConfigKeys::kLogLevel, "info");
        config.setBool(ConfigKeys::kLogToConsole, true);
        config.setBool(ConfigKeys::kLogToFile, true);
        config.setInt(ConfigKeys::kLogMaxSizeMB, 16);

        // GPU
        config.setString(ConfigKeys::kPreferredGpu, "auto");
        config.setBool(ConfigKeys::kEnableNvidia, true);
        config.setBool(ConfigKeys::kEnableIntel, true);
        config.setBool(ConfigKeys::kEnableTensorCores, true);
        config.setBool(ConfigKeys::kEnableXmx, true);
        config.setBool(ConfigKeys::kEnableHwFlow, true);

        // 帧生成
        config.setBool(ConfigKeys::kFrameGenEnabled, true);
        config.setInt(ConfigKeys::kFrameGenMultiplier, 2);
        config.setInt(ConfigKeys::kFrameGenQuality, 1);  // Balanced
        config.setBool(ConfigKeys::kFrameGenOcclusionAware, true);
        config.setBool(ConfigKeys::kFrameGenDeJelly, true);
        config.setFloat(ConfigKeys::kFrameGenDeJellyStrength, 0.6);
        config.setBool(ConfigKeys::kFrameGenAiRepair, true);
        config.setBool(ConfigKeys::kFrameGenTemporalSmooth, true);
        config.setFloat(ConfigKeys::kFrameGenTemporalWeight, 0.15);
        config.setBool(ConfigKeys::kFrameGenAdaptiveQuality, true);
        config.setFloat(ConfigKeys::kFrameGenMaxFrameTimeMs, 8.0);

        // 淮竹
        config.setBool(ConfigKeys::kHuaiZhuEnabled, true);
        config.setInt(ConfigKeys::kWarmupFrames, 30);
        config.setFloat(ConfigKeys::kConvergenceThreshold, 0.95);
        config.setInt(ConfigKeys::kDepthStride, 2);
        config.setFloat(ConfigKeys::kDefaultFov, 78.0);

        // 捕获
        config.setString(ConfigKeys::kCaptureBackend, "auto");
        config.setBool(ConfigKeys::kCaptureCursor, false);
        config.setInt(ConfigKeys::kCaptureFpsLimit, 0);  // 不限

        // 学习
        config.setBool(ConfigKeys::kLearningEnabled, true);
        config.setString(ConfigKeys::kLearningDbPath, "");
        config.setBool(ConfigKeys::kLearningAutoSave, true);
        config.setBool(ConfigKeys::kLearningCrowdEnabled, false);

        // AI
        config.setString(ConfigKeys::kAiModelPath, "");
        config.setString(ConfigKeys::kAiDevice, "auto");
        config.setString(ConfigKeys::kAiPrecision, "fp16");
        config.setString(ConfigKeys::kAiCacheDir, "");

        // UI
        config.setFloat(ConfigKeys::kUiOpacity, 1.0);
        config.setBool(ConfigKeys::kUiAlwaysOnTop, false);
        config.setBool(ConfigKeys::kUiShowPerformance, true);
        config.setBool(ConfigKeys::kUiWallpaperEnabled, true);
        config.setString(ConfigKeys::kUiWallpaperPath, "");
        config.setBool(ConfigKeys::kUiRippleEnabled, true);
        config.setFloat(ConfigKeys::kUiRippleStrength, 0.8);

        // 热键
        config.setString(ConfigKeys::kHotkeyToggle, "Ctrl+Shift+F");
        config.setString(ConfigKeys::kHotkeyPanel, "Ctrl+Shift+P");
        config.setString(ConfigKeys::kHotkeyScreenshot, "Ctrl+Shift+S");
    }

    void Config::resetToDefaults() {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.clear();
        populateDefaults(*this);
    }

} // namespace Lingjing
