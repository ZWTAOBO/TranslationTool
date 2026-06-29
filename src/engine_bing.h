#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include "translation_engine.h"
#include "engine_windows.h"  // for HttpResponse
#include <string>
#include <vector>
#include <chrono>

// Bing Translator engine — uses the hidden API at cn.bing.com/ttranslatev3.
// No API key required. Works in China (uses cn.bing.com).
// Internally fetches the translator page to extract IG/IID/token, then
// makes POST requests for each translation. Tokens are refreshed when expired.
class BingEngine : public TranslationEngine {
public:
    BingEngine();
    ~BingEngine();

    TranslationResult Translate(const std::string& text,
                                const std::string& from,
                                const std::string& to) override;
    bool Available() const override;
    std::string Name() const override;

private:
    // ── Token data extracted from the Bing Translator page ────────────────
    struct BingToken {
        std::string ig;
        std::string iid;
        std::string token;
        std::string key;
        std::chrono::steady_clock::time_point fetchTime;
        int expiryIntervalMs = 0;
        int requestCount = 0;
    };

    // ── HTTP helpers ─────────────────────────────────────────────────────
    std::string GetPage(const std::string& url);
    HttpResponse PostForm(const std::string& host, const std::string& path,
                          const std::string& formBody,
                          const std::string& referer);

    // ── Token management ─────────────────────────────────────────────────
    bool FetchTokens();
    bool TokensValid() const;
    std::string MakeTranslateURL() const;
    std::string MakeFormBody(const std::string& text,
                             const std::string& from,
                             const std::string& to) const;

    // ── Page parsing ─────────────────────────────────────────────────────
    std::string ExtractBetween(const std::string& html,
                               const std::string& start,
                               const std::string& end,
                               size_t offset = 0) const;

    // ── Language code mapping ────────────────────────────────────────────
    static std::string LangCodeToBing(const std::string& code);
    static std::string LangCodeFromBing(const std::string& bingCode);

    BingToken token_;
    bool initialized_ = false;
    HINTERNET sharedSession_ = nullptr;
};
