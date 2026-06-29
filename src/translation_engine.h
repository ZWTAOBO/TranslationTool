#pragma once
#include <string>

struct TranslationResult {
    std::string srcText;
    std::string dstText;
    std::string detectedLang;
    std::string engineName;   // Name of the engine that produced this result
    bool success = false;
    std::string errorMsg;
};

class TranslationEngine {
public:
    virtual ~TranslationEngine() = default;

    // Translate text from source language to target language.
    // from/to use app-level codes (same as Baidu codes: "auto", "en", "zh", "jp", etc.)
    virtual TranslationResult Translate(const std::string& text,
                                        const std::string& from,
                                        const std::string& to) = 0;

    // Whether this engine can be used right now.
    virtual bool Available() const = 0;

    // Human-readable engine name for UI.
    virtual std::string Name() const = 0;
};
