#pragma once
#include "translation_engine.h"
#include <string>

class YoudaoEngine : public TranslationEngine {
public:
    YoudaoEngine() = default;

    void SetCredentials(const std::string& appId, const std::string& apiKey);

    TranslationResult Translate(const std::string& text,
                                const std::string& from,
                                const std::string& to) override;
    bool Available() const override;
    std::string Name() const override;

private:
    std::string CalculateSHA256(const std::string& input);
    std::string MakePostRequest(const std::string& host, const std::string& path,
                                const std::string& formBody);
    std::string LangCodeToYoudao(const std::string& code);

    std::string appId_;
    std::string apiKey_;
};
