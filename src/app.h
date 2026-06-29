#pragma once
#include <windows.h>
#include "config.h"
#include "translator.h"
#include "history.h"
#include "clipboard.h"
#include "ui.h"

class App {
public:
    App();
    ~App();

    void LoadConfig();
    void SaveConfig();
    const AppConfig& IsConfig() const { return config_.Get(); }
    AppConfig& GetConfig() { return config_.Get(); }

    void SetWindowHandle(HWND hwnd);
    HWND GetWindowHandle() const { return hwnd_; }
    void HideWindow();
    void ShowSettings();

    void OnHotkeyTriggered();
    void Render();

    void SetStatus(const std::string& status, bool isError = false);

private:
    void PerformTranslation(const std::string& text, const std::string& targetLang);

    Config config_;
    Translator translator_;
    History history_;
    Clipboard clipboard_;
    UI ui_;

    HWND hwnd_ = nullptr;

    // Translate state
    bool isTranslating_ = false;
};
