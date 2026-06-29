#include "translator.h"
#include "engine_baidu.h"
#include "engine_youdao.h"
#include "engine_bing.h"
#include "engine_windows.h"

#include <vector>

const char* Translator::kLanguageList[18] = {
    "自动检测", "中文", "英文", "日文", "韩文", "法文", "德文",
    "西班牙文", "葡萄牙文", "俄文", "阿拉伯文", "意大利文", "荷兰文",
    "泰文", "越南文", "波兰文", "希腊文", "瑞典文"
};

const char* Translator::kLanguageCodes[18] = {
    "auto", "zh", "en", "jp", "kor", "fra", "de",
    "spa", "pt", "ru", "ara", "it", "nl",
    "th", "vie", "pl", "el", "swe"
};

const char* Translator::kTargetLanguageList[17] = {
    "中文", "英文", "日文", "韩文", "法文", "德文",
    "西班牙文", "葡萄牙文", "俄文", "阿拉伯文", "意大利文", "荷兰文",
    "泰文", "越南文", "波兰文", "希腊文", "瑞典文"
};

const char* Translator::kTargetLanguageCodes[17] = {
    "zh", "en", "jp", "kor", "fra", "de",
    "spa", "pt", "ru", "ara", "it", "nl",
    "th", "vie", "pl", "el", "swe"
};

Translator::Translator()
    : baiduEngine_(std::make_unique<BaiduEngine>())
    , youdaoEngine_(std::make_unique<YoudaoEngine>())
    , bingEngine_(std::make_unique<BingEngine>())
    , windowsEngine_(std::make_unique<WindowsEngine>())
{
}

Translator::~Translator() = default;

void Translator::SetCredentials(const std::string& appId, const std::string& apiKey) {
    baiduEngine_->SetCredentials(appId, apiKey);
    hasBaiduConfig_ = !appId.empty() && !apiKey.empty();
}

void Translator::SetYoudaoCredentials(const std::string& appId, const std::string& apiKey) {
    youdaoEngine_->SetCredentials(appId, apiKey);
    hasYoudaoConfig_ = !appId.empty() && !apiKey.empty();
}

bool Translator::HasBaiduConfig() const { return hasBaiduConfig_; }
bool Translator::HasYoudaoConfig() const { return hasYoudaoConfig_; }

void Translator::SetTargetLang(const std::string& lang) { targetLang_ = lang; }
void Translator::SetFromLang(const std::string& lang) { fromLang_ = lang; }

std::string Translator::GetActiveEngineName() const {
    if (hasBaiduConfig_ && baiduEngine_->Available())
        return baiduEngine_->Name();
    if (hasYoudaoConfig_ && youdaoEngine_->Available())
        return youdaoEngine_->Name();
    if (bingEngine_->Available())
        return bingEngine_->Name();
    if (windowsEngine_->Available())
        return windowsEngine_->Name();
    return "无可用引擎";
}

// ── Core translate: engine routing ──────────────────────────────────────────

TranslationResult Translator::Translate(const std::string& text) {
    TranslationResult result;
    result.srcText = text;

    if (text.empty()) {
        result.success = false;
        result.errorMsg = "请输入要翻译的内容";
        return result;
    }

    // Strategy:
    //   1. Baidu (if configured, most reliable)
    //   2. Youdao (if configured, good fallback)
    //   3. Bing (free, works in China)
    //   4. Windows/MyMemory (last resort, may be blocked in China)
    struct EngineSlot {
        TranslationEngine* engine;
        const char* name;
        bool active;  // true = always try; false = skipped unless configured
    };

    EngineSlot slots[] = {
        { baiduEngine_.get(),   "百度翻译",              hasBaiduConfig_ },
        { youdaoEngine_.get(),  "有道翻译",              hasYoudaoConfig_ },
        { bingEngine_.get(),    "必应翻译 (Bing)",       true },
        { windowsEngine_.get(), "在线翻译 (MyMemory)",   true },
    };

    std::vector<std::string> engineErrors;

    for (auto& slot : slots) {
        if (!slot.active) continue;
        if (!slot.engine->Available()) continue;

        auto engResult = slot.engine->Translate(text, fromLang_, targetLang_);
        if (engResult.success) {
            engResult.engineName = slot.name;
            return engResult;
        }
        engineErrors.push_back(std::string(slot.name) + ": " + engResult.errorMsg);
    }

    // ── All engines failed ──
    result.errorMsg.clear();
    for (size_t i = 0; i < engineErrors.size(); i++) {
        if (i > 0) result.errorMsg += "\n";
        result.errorMsg += engineErrors[i];
    }

    if (engineErrors.empty()) {
        result.errorMsg = "未配置翻译引擎。请在设置中配置百度翻译或有道翻译";
    } else if (!hasBaiduConfig_ && !hasYoudaoConfig_) {
        result.errorMsg += "\n提示: 配置百度翻译或有道翻译可解决此问题";
    }

    result.success = false;
    return result;
}

// ── Test Baidu credentials (bypass routing) ─────────────────────────────────

TranslationResult Translator::TestBaiduCredentials() {
    TranslationResult result;
    if (!hasBaiduConfig_ || !baiduEngine_->Available()) {
        result.success = false;
        result.errorMsg = "请先填写 App ID 和密钥";
        return result;
    }

    auto engResult = baiduEngine_->Translate("Hello", "auto", "zh");
    engResult.engineName = baiduEngine_->Name();
    return engResult;
}

TranslationResult Translator::TestYoudaoCredentials() {
    TranslationResult result;
    if (!hasYoudaoConfig_ || !youdaoEngine_->Available()) {
        result.success = false;
        result.errorMsg = "请先填写有道翻译 App ID 和密钥";
        return result;
    }

    auto engResult = youdaoEngine_->Translate("Hello", "auto", "zh");
    engResult.engineName = youdaoEngine_->Name();
    return engResult;
}

// ── Static language list helpers ────────────────────────────────────────────

const char** Translator::GetLanguageList() { return kLanguageList; }

int Translator::GetLanguageCount() { return 18; }

std::string Translator::GetLangCode(int index) {
    if (index >= 0 && index < 18)
        return kLanguageCodes[index];
    return "auto";
}

int Translator::GetLangIndex(const std::string& code) {
    for (int i = 0; i < 18; i++) {
        if (kLanguageCodes[i] == code)
            return i;
    }
    return 0;
}

const char** Translator::GetTargetLanguageList() { return kTargetLanguageList; }

int Translator::GetTargetLanguageCount() { return 17; }

std::string Translator::GetTargetLangCode(int index) {
    if (index >= 0 && index < 17)
        return kTargetLanguageCodes[index];
    return "zh";
}

int Translator::GetTargetLangIndex(const std::string& code) {
    for (int i = 0; i < 17; i++) {
        if (kTargetLanguageCodes[i] == code)
            return i;
    }
    return 0;
}
