#pragma once
#include <string>
#include <nlohmann/json.hpp>

struct AppConfig {
    std::string appId = "";           // Baidu App ID
    std::string apiKey = "";          // Baidu API Key
    std::string youdaoAppId = "";     // Youdao App ID
    std::string youdaoApiKey = "";    // Youdao API Key (secret)
    std::string targetLang = "zh";    // Default target language
    int windowOpacity = 200;          // Window opacity (0-255)
    int windowWidth = 440;            // Window width
    int windowHeight = 500;           // Window height
    int windowX = -1;                 // Window X position (-1 = center on next show)
    int windowY = -1;                 // Window Y position (-1 = center on next show)
    bool alwaysOnTop = true;          // Always on top
    bool autoHideOnFocusLost = false; // Auto-hide on focus lost
    int maxHistorySize = 500;         // Max history entries
    bool autoStart = false;           // Auto-start on Windows boot
};

inline void to_json(nlohmann::json& j, const AppConfig& c) {
    j = nlohmann::json{
        {"appId", c.appId}, {"apiKey", c.apiKey},
        {"youdaoAppId", c.youdaoAppId}, {"youdaoApiKey", c.youdaoApiKey},
        {"targetLang", c.targetLang},
        {"windowOpacity", c.windowOpacity},
        {"windowWidth", c.windowWidth}, {"windowHeight", c.windowHeight},
        {"windowX", c.windowX}, {"windowY", c.windowY},
        {"alwaysOnTop", c.alwaysOnTop},
        {"autoHideOnFocusLost", c.autoHideOnFocusLost},
        {"maxHistorySize", c.maxHistorySize},
        {"autoStart", c.autoStart}
    };
}

inline void from_json(const nlohmann::json& j, AppConfig& c) {
    if (j.contains("appId")) j.at("appId").get_to(c.appId);
    if (j.contains("apiKey")) j.at("apiKey").get_to(c.apiKey);
    if (j.contains("youdaoAppId")) j.at("youdaoAppId").get_to(c.youdaoAppId);
    if (j.contains("youdaoApiKey")) j.at("youdaoApiKey").get_to(c.youdaoApiKey);
    if (j.contains("targetLang")) j.at("targetLang").get_to(c.targetLang);
    if (j.contains("windowOpacity")) j.at("windowOpacity").get_to(c.windowOpacity);
    if (j.contains("windowWidth")) j.at("windowWidth").get_to(c.windowWidth);
    if (j.contains("windowHeight")) j.at("windowHeight").get_to(c.windowHeight);
    if (j.contains("windowX")) j.at("windowX").get_to(c.windowX);
    if (j.contains("windowY")) j.at("windowY").get_to(c.windowY);
    if (j.contains("alwaysOnTop")) j.at("alwaysOnTop").get_to(c.alwaysOnTop);
    if (j.contains("autoHideOnFocusLost")) j.at("autoHideOnFocusLost").get_to(c.autoHideOnFocusLost);
    if (j.contains("maxHistorySize")) j.at("maxHistorySize").get_to(c.maxHistorySize);
    if (j.contains("autoStart")) j.at("autoStart").get_to(c.autoStart);
}

class Config {
public:
    Config();
    ~Config();

    void Load();
    void Save();
    const AppConfig& Get() const { return config_; }
    AppConfig& Get() { return config_; }
    void Reset();

private:
    AppConfig config_;
    std::string filePath_;
    std::wstring exePath_;
};
