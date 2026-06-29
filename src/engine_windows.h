#pragma once
#include "translation_engine.h"
#include <string>
#include <vector>

// Simple HTTP response container
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string winHttpError; // diagnostic if WinHTTP call fails
};

// Free web translation engine — uses MyMemory API (api.mymemory.translated.net).
// No API key or configuration required. Requires internet.
class WindowsEngine : public TranslationEngine {
public:
    WindowsEngine() = default;

    TranslationResult Translate(const std::string& text,
                                const std::string& from,
                                const std::string& to) override;
    bool Available() const override;
    std::string Name() const override;

private:
    HttpResponse MakeRequest(const std::string& url);

    // Map app language codes to MyMemory ISO language codes
    static std::string LangCodeToMyMemory(const std::string& code);
    static std::vector<std::string> SplitLines(const std::string& text);
};
