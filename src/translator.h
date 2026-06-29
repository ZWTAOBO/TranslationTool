#pragma once
#include <string>
#include <memory>

#include "translation_engine.h"

class BaiduEngine;
class BingEngine;
class YoudaoEngine;
class WindowsEngine;

class Translator {
public:
    Translator();
    ~Translator();

    void SetCredentials(const std::string& appId, const std::string& apiKey);
    void SetYoudaoCredentials(const std::string& appId, const std::string& apiKey);
    void SetTargetLang(const std::string& lang);
    void SetFromLang(const std::string& lang);

    TranslationResult Translate(const std::string& text);

    // Engine info for UI
    std::string GetActiveEngineName() const;
    bool HasBaiduConfig() const;
    bool HasYoudaoConfig() const;

    // Test credentials directly (bypasses engine routing)
    TranslationResult TestBaiduCredentials();
    TranslationResult TestYoudaoCredentials();

    // Language list (shared across engines)
    static const char** GetLanguageList();
    static int GetLanguageCount();
    static std::string GetLangCode(int index);
    static int GetLangIndex(const std::string& code);

    // Target-only language list (no "auto-detect")
    static const char** GetTargetLanguageList();
    static int GetTargetLanguageCount();
    static std::string GetTargetLangCode(int index);
    static int GetTargetLangIndex(const std::string& code);

private:
    std::unique_ptr<BaiduEngine> baiduEngine_;
    std::unique_ptr<YoudaoEngine> youdaoEngine_;
    std::unique_ptr<BingEngine> bingEngine_;
    std::unique_ptr<WindowsEngine> windowsEngine_;
    bool hasBaiduConfig_ = false;
    bool hasYoudaoConfig_ = false;

    std::string targetLang_ = "zh";
    std::string fromLang_ = "auto";

    static const char* kLanguageList[18];
    static const char* kLanguageCodes[18];
    static const char* kTargetLanguageList[17];
    static const char* kTargetLanguageCodes[17];
};
