#include "app.h"
#include "utils.h"
#include <string>
#include <windows.h>
#include <objbase.h>

App::App()
    : ui_(&translator_, &history_, &config_)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // Setup close callback
    ui_.SetCloseCallback([this]() { HideWindow(); });

    // Setup opacity callback (hwnd_ not yet set; bound on SetWindowHandle)
    ui_.SetOpacityCallback([this](int opacity) {
        if (hwnd_) {
            SetLayeredWindowAttributes(hwnd_, 0, (BYTE)opacity, LWA_ALPHA);
        }
    });

}

App::~App() {
    CoUninitialize();
}

void App::LoadConfig() {
    config_.Load();

    const auto& cfg = config_.Get();

    // Initialize translator with stored credentials
    translator_.SetCredentials(cfg.appId, cfg.apiKey);
    translator_.SetYoudaoCredentials(cfg.youdaoAppId, cfg.youdaoApiKey);
    translator_.SetTargetLang(cfg.targetLang);

    // Determine executable directory for history file
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ws(path);
    std::string exePath = WStringToUtf8(ws);
    size_t pos = exePath.find_last_of('\\');
    if (pos != std::string::npos)
        exePath = exePath.substr(0, pos);
    std::string historyPath = exePath + "\\history.json";

    history_.SetMaxSize(cfg.maxHistorySize);
    history_.Load(historyPath);
}

void App::SaveConfig() {
    config_.Save();
}

void App::SetWindowHandle(HWND hwnd) {
    hwnd_ = hwnd;
    ui_.SetWindowHandle(hwnd);
    // Sync initial window size to UI (matches current Win32 window)
    const auto& cfg = config_.Get();
    ui_.SetWindowSize(cfg.windowWidth, cfg.windowHeight);
}

void App::HideWindow() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void App::ShowSettings() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        BringWindowToTop(hwnd_);
        SetForegroundWindow(hwnd_);
    }
    ui_.ShowSettings();
}

void App::OnHotkeyTriggered() {
    if (isTranslating_)
        return;

    isTranslating_ = true;
    SetStatus("正在获取选中文本...");

    // Get selected text FIRST — before showing our window —
    // so the foreground window is still the user's active app (Notepad, browser, etc.)
    // and direct EM_GETSEL read or SendInput Ctrl+C target the correct window.
    std::wstring selectedText = clipboard_.GetSelectedText();
    std::string text = WStringToUtf8(selectedText);

    // Show the window after capturing text
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        BringWindowToTop(hwnd_);
        SetForegroundWindow(hwnd_);
    }

    if (text.empty()) {
        SetStatus("未检测到选中文本，请先选中文字再按快捷键", true);
        isTranslating_ = false;
        return;
    }

    // Set the text and auto-translate
    ui_.SetTranslationInput(text);
    SetStatus("正在翻译...");
    std::string targetLang = Translator::GetTargetLangCode(ui_.GetTargetLangIndex());
    PerformTranslation(text, targetLang);

    // Defensive: ensure input text survives to render
    ui_.SetTranslationInput(text);

    isTranslating_ = false;
}

void App::PerformTranslation(const std::string& text, const std::string& targetLang) {
    if (text.empty()) return;

    translator_.SetTargetLang(targetLang);

    auto result = translator_.Translate(text);

    if (result.success) {
        ui_.OnTranslateResult(text, result.dstText, result.detectedLang, result.engineName);
        history_.AddEntry(result.srcText, result.dstText, result.detectedLang, targetLang);
        SetStatus("翻译完成");
    } else {
        ui_.OnTranslateResult("", "", "", "");
        SetStatus(result.errorMsg, true);
    }
}

void App::Render() {
    ui_.Render();
}

void App::SetStatus(const std::string& status, bool isError) {
    ui_.SetStatus(status, isError);
}

