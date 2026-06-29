#pragma once
#include "translation_engine.h"
#include <string>

class BaiduEngine : public TranslationEngine {
public:
    BaiduEngine() = default;

    void SetCredentials(const std::string& appId, const std::string& apiKey);

    TranslationResult Translate(const std::string& text,
                                const std::string& from,
                                const std::string& to) override;
    bool Available() const override;
    std::string Name() const override;

private:
    std::string CalculateMD5(const std::string& input);
    std::string MakeRequest(const std::string& url);

    std::string appId_;
    std::string apiKey_;
};
