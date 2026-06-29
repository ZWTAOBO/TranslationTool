#pragma once
#include <string>
#include <functional>
#include <windows.h>
#include <imgui.h>

// Forward declarations
class Translator;
class History;
class Config;
struct AppConfig;

class UI {
public:
    UI(Translator* translator, History* history, Config* config);
    ~UI();

    void Render();
    void SetStatus(const std::string& status, bool isError = false);
    void SetTranslationInput(const std::string& text);

    enum class ViewMode {
        Main,     // Main translation view
        History,  // History browser
        Settings  // Settings panel
    };

    void SetViewMode(ViewMode mode);
    ViewMode GetViewMode() const { return viewMode_; }
    int GetTargetLangIndex() const { return toLangIndex_; }
    void ShowSettings();

    // Callbacks
    void OnTranslateResult(const std::string& src, const std::string& dst,
                           const std::string& detectedLang,
                           const std::string& engineName = "");

    // Set callback for window close action
    void SetCloseCallback(std::function<void()> cb) { closeCallback_ = cb; }

    // Set callback to apply window opacity at runtime
    void SetOpacityCallback(std::function<void(int)> cb) { opacityCallback_ = cb; }

    // Set parent window handle (for dragging)
    void SetWindowHandle(HWND hwnd) { hwnd_ = hwnd; }
    void SetWindowSize(int w, int h) { curW_ = w; curH_ = h; }

private:
    void RenderMainView();
    void RenderHistoryView();
    void RenderSettingsView();

    void DoTranslate();

    Translator* translator_;
    History* history_;
    Config* config_;

    ViewMode viewMode_ = ViewMode::Main;

    char inputBuf_[16384] = {};
    char resultBuf_[32768] = {};
    char detectedLangBuf_[64] = {};
    char engineNameBuf_[64] = {};
    char statusBuf_[256] = {};
    bool statusIsError_ = false;

    int fromLangIndex_ = 0;    // Index in language list (0 = auto)
    int toLangIndex_ = 0;      // Index in target language list (0 = zh)

    // History view state
    char historyFilter_[128] = {};
    int selectedHistoryIndex_ = -1;

    // Settings view state
    char settingsAppId_[256] = {};
    char settingsApiKey_[256] = {};
    char settingsYoudaoAppId_[256] = {};
    char settingsYoudaoApiKey_[256] = {};
    int settingsOpacity_ = 200;
    int settingsTargetLang_ = 0;
    bool settingsAutoStart_ = false;
    bool apiVerifyResult_ = false;
    bool apiVerifyDone_ = false;
    bool youdaoApiVerifyDone_ = false;
    bool youdaoApiVerifyResult_ = false;

    // Current window size (dynamic, resize grip)
    int curW_ = 440;
    int curH_ = 500;

    std::function<void()> closeCallback_;
    std::function<void(int)> opacityCallback_;
    HWND hwnd_ = nullptr;
};
