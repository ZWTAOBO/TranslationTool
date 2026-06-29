#include "ui.h"
#include "translator.h"
#include "history.h"
#include "config.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <algorithm>

// Updated by UI::Render() each frame; read by main loop for resize click detection
// 0=none, 1=bottom-right, 2=bottom-left
extern int g_ResizeCorner;

// Helper: show hand cursor on hover for clickable items
static inline void HandCursor() {
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

UI::UI(Translator* translator, History* history, Config* config)
    : translator_(translator), history_(history), config_(config)
{
    const auto& cfg = config_->Get();
    toLangIndex_ = Translator::GetTargetLangIndex(cfg.targetLang);
    fromLangIndex_ = 0; // auto

        strcpy_s(settingsAppId_, cfg.appId.c_str());
        strcpy_s(settingsApiKey_, cfg.apiKey.c_str());
        strcpy_s(settingsYoudaoAppId_, cfg.youdaoAppId.c_str());
        strcpy_s(settingsYoudaoApiKey_, cfg.youdaoApiKey.c_str());
        settingsOpacity_ = cfg.windowOpacity;
        settingsTargetLang_ = toLangIndex_;
    curW_ = cfg.windowWidth;
    curH_ = cfg.windowHeight;
}

UI::~UI() = default;

void UI::SetStatus(const std::string& status, bool isError) {
    strcpy_s(statusBuf_, status.c_str());
    statusIsError_ = isError;
}

void UI::SetTranslationInput(const std::string& text) {
    strcpy_s(inputBuf_, text.c_str());
}

void UI::OnTranslateResult(const std::string& src, const std::string& dst,
                           const std::string& detectedLang,
                           const std::string& engineName) {
    strcpy_s(resultBuf_, dst.c_str());
    strcpy_s(detectedLangBuf_, detectedLang.c_str());
    strcpy_s(engineNameBuf_, engineName.c_str());

    // Also set the result into the input area for follow-up translations
    // Actually, keep input as-is and just update the result
}

void UI::ShowSettings() {
    const auto& cfg = config_->Get();
    strcpy_s(settingsAppId_, cfg.appId.c_str());
    strcpy_s(settingsApiKey_, cfg.apiKey.c_str());
    settingsOpacity_ = cfg.windowOpacity;
    settingsAutoStart_ = cfg.autoStart;
    settingsTargetLang_ = toLangIndex_;
    SetViewMode(ViewMode::Settings);
}

void UI::Render() {
    // Win32 window is the "outermost frame" — source of truth for size
    if (hwnd_) {
        RECT cr;
        GetClientRect(hwnd_, &cr);
        curW_ = cr.right;
        curH_ = cr.bottom;
    }

    // ImGui window always fills the entire Win32 client area
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)curW_, (float)curH_), ImGuiCond_Always);

    // Reduce inner padding to maximise content space
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoResize;

    // Track hover state for drag (resize tracking done in main loop)
    bool hoverTitle = false;

    if (ImGui::Begin("TranslationTool", nullptr, flags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float barH = 30.0f;
        float barW = ImGui::GetContentRegionAvail().x;
        float padX = ImGui::GetStyle().WindowPadding.x;
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // === Draggable title bar (shared across all views) ===
        dl->AddRectFilled(origin, ImVec2(origin.x + barW, origin.y + barH),
                         IM_COL32(10, 10, 25, 220), 5.0f,
                         ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
        dl->AddText(ImVec2(origin.x + padX, origin.y + 6),
                    IM_COL32(180, 184, 210, 255), "翻译工具");

        // Drag handle (visual only — actual drag uses SetCapture)
        ImGui::InvisibleButton("##winDrag", ImVec2(barW - 34, barH));
        hoverTitle = ImGui::IsItemHovered();
        if (hoverTitle)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        // Close button
        ImGui::SetCursorScreenPos(ImVec2(origin.x + barW - 28, origin.y + 3));
        if (ImGui::SmallButton("×")) {
            if (closeCallback_) closeCallback_();
        }
        HandCursor();

        // Content divider
        ImGui::SetCursorPos(ImVec2(0, barH + 2));
        ImGui::Separator();

        // === Content area ===
        switch (viewMode_) {
        case ViewMode::Main:
            RenderMainView();
            break;
        case ViewMode::History:
            RenderHistoryView();
            break;
        case ViewMode::Settings:
            RenderSettingsView();
            break;
        }

        // === Resize handles (bottom corners) ===
        // Visual triangles + hit-zone detection (cursor change via ImGui)
        {
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            float rh = 16.0f;
            float margin = 8.0f;
            ImVec2 mousePos = ImGui::GetMousePos();

            // Bottom-right triangle
            dl->AddTriangleFilled(
                ImVec2(wp.x + ws.x - rh, wp.y + ws.y),
                ImVec2(wp.x + ws.x, wp.y + ws.y),
                ImVec2(wp.x + ws.x, wp.y + ws.y - rh),
                IM_COL32(140, 140, 180, 150));

            // Bottom-left triangle (mirrored)
            dl->AddTriangleFilled(
                ImVec2(wp.x, wp.y + ws.y),
                ImVec2(wp.x + rh, wp.y + ws.y),
                ImVec2(wp.x, wp.y + ws.y - rh),
                IM_COL32(140, 140, 180, 150));

            // Corner zone detection for main-loop resize tracking
            bool cornerBR = (mousePos.x >= wp.x + ws.x - margin &&
                             mousePos.x < wp.x + ws.x &&
                             mousePos.y >= wp.y + ws.y - margin &&
                             mousePos.y < wp.y + ws.y);
            bool cornerBL = (mousePos.x >= wp.x &&
                             mousePos.x < wp.x + margin &&
                             mousePos.y >= wp.y + ws.y - margin &&
                             mousePos.y < wp.y + ws.y);

            g_ResizeCorner = cornerBR ? 1 : (cornerBL ? 2 : 0);

            if (cornerBR)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            else if (cornerBL)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // === Manual drag tracking (real-time via GetAsyncKeyState + SetCapture) ===
    bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // Drag state
    static bool dragging = false;
    static POINT dragStart;
    static int dragBaseX, dragBaseY;

    if (!dragging && clicked && hoverTitle) {
        dragging = true;
        SetCapture(hwnd_);
        GetCursorPos(&dragStart);
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        dragBaseX = wr.left;
        dragBaseY = wr.top;
    }
    if (dragging && !mouseDown) {
        dragging = false;
        ReleaseCapture();
    }
    if (dragging) {
        POINT cur;
        GetCursorPos(&cur);
        SetWindowPos(hwnd_, nullptr,
                     dragBaseX + (cur.x - dragStart.x),
                     dragBaseY + (cur.y - dragStart.y),
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void UI::RenderMainView() {
    // === Dynamic layout: measure remaining space for result area after
    // placing input and buttons, so the status bar is always visible. ===
    // Input takes ~20% of window height (min 60px).
    float inputH = max(60.0f, (float)curH_ * 0.20f);

    // Top bar: language selection + settings button
    ImGui::BeginGroup();
    {
        // Source language combo
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##fromLang", &fromLangIndex_, Translator::GetLanguageList(), Translator::GetLanguageCount())) {
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("→");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##toLang", &toLangIndex_, Translator::GetTargetLanguageList(), Translator::GetTargetLanguageCount())) {
        }

        // Quick toggle: single character, opposite of current target
        ImGui::SameLine();
        const char* toggleLabel = (toLangIndex_ == 0) ? "英" : "中";
        if (ImGui::SmallButton(toggleLabel)) {
            toLangIndex_ = (toLangIndex_ == 0) ? 1 : 0;
        }
        HandCursor();

        // Settings button (far right, aligned to content edge)
        float contentRight = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        ImGui::SameLine(contentRight - 48);
        if (ImGui::Button("设置", ImVec2(40, 0)))
        {
            const auto& cfg = config_->Get();
            strcpy_s(settingsAppId_, cfg.appId.c_str());
            strcpy_s(settingsApiKey_, cfg.apiKey.c_str());
            settingsOpacity_ = cfg.windowOpacity;
            settingsAutoStart_ = cfg.autoStart;
            settingsTargetLang_ = toLangIndex_;
            SetViewMode(ViewMode::Settings);
        }
        HandCursor();
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // Input text area (dynamic height)
    ImGui::InputTextMultiline("##input", inputBuf_, sizeof(inputBuf_),
                              ImVec2(-1, inputH),
                              ImGuiInputTextFlags_AllowTabInput);
    // Placeholder hint when empty
    if (inputBuf_[0] == '\0' && !ImGui::IsItemActive()) {
        ImVec2 pos = ImGui::GetItemRectMin();
        pos.x += 6.0f; pos.y += 6.0f;
        ImGui::GetWindowDrawList()->AddText(pos, IM_COL32(160,160,160,200),
                                            "输入文本，或按 Ctrl+Enter 翻译划词");
    }

    // Translate button row
    bool hasInput = strlen(inputBuf_) > 0;
    if (!hasInput) ImGui::BeginDisabled();

    if (ImGui::Button("翻译", ImVec2(80, 28))) {
        DoTranslate();
    }
    HandCursor();

    if (!hasInput) ImGui::EndDisabled();

    // Handle Ctrl+Enter shortcut to trigger translation
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (strlen(inputBuf_) > 0) {
            DoTranslate();
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Enter");

    if (strlen(detectedLangBuf_) > 0) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        ImGui::TextDisabled("检测到: %s", detectedLangBuf_);
    }

    ImGui::Separator();

    // Calculate result height from remaining space (after input + buttons),
    // reserving room for the bottom bar + status so nothing overflows.
    {
        bool hasStatus = strlen(statusBuf_) > 0;
        float bottomH = 50.0f;   // buttons + separators + padding
        if (hasStatus) bottomH += 28.0f;  // status separator + text
        float resultH = max(30.0f, ImGui::GetContentRegionAvail().y - bottomH);

    // Result area with background (fills remaining space)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.18f, 0.6f));
    ImGui::BeginChild("##resultArea", ImVec2(-1, resultH), true);
    {
        if (strlen(resultBuf_) > 0) {
            ImGui::TextWrapped("%s", resultBuf_);
            if (strlen(engineNameBuf_) > 0) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.5f, 0.6f, 0.8f));
                ImGui::Text("— %s", engineNameBuf_);
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::TextDisabled("翻译结果将显示在这里...");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    }  // end result-height scope

    // Bottom bar: action buttons
    ImGui::Separator();

    if (ImGui::SmallButton("历史")) {
        SetViewMode(ViewMode::History);
    }
    HandCursor();

    ImGui::SameLine();

    if (strlen(resultBuf_) > 0) {
        if (ImGui::SmallButton("复制结果")) {
            ImGui::SetClipboardText(resultBuf_);
            SetStatus("已复制到剪贴板");
        }
        HandCursor();
        ImGui::SameLine();
    }

    if (strlen(inputBuf_) > 0) {
        if (ImGui::SmallButton("清空")) {
            inputBuf_[0] = '\0';
            resultBuf_[0] = '\0';
            detectedLangBuf_[0] = '\0';
        }
        HandCursor();
        ImGui::SameLine();
    }

    // Auto-hide on focus lost toggle (always visible)
    {
        bool autoHide = config_->Get().autoHideOnFocusLost;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.7f, 0.9f));
        if (ImGui::SmallButton(autoHide ? "失焦隐藏:开" : "失焦隐藏:关")) {
            config_->Get().autoHideOnFocusLost = !autoHide;
            config_->Save();
        }
        HandCursor();
        ImGui::PopStyleColor();
    }

    // Status bar
    if (strlen(statusBuf_) > 0) {
        ImGui::Separator();
        if (statusIsError_) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            ImGui::TextUnformatted(statusBuf_);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
            ImGui::TextUnformatted(statusBuf_);
            ImGui::PopStyleColor();
        }
    }
}

void UI::RenderHistoryView() {
    // Header with back button
    ImGui::BeginGroup();
    {
        if (ImGui::SmallButton("← 返回")) {
            SetViewMode(ViewMode::Main);
        }
        HandCursor();
        ImGui::SameLine();
        ImGui::TextUnformatted("翻译历史");

        ImGui::SameLine(ImGui::GetWindowWidth() - 80);
        if (history_->GetCount() > 0 && ImGui::SmallButton("清空全部")) {
            history_->Clear();
            selectedHistoryIndex_ = -1;
        }
        HandCursor();
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // Search filter
    ImGui::InputTextWithHint("##filter", "搜索历史...", historyFilter_, sizeof(historyFilter_));
    ImGui::Separator();

    // History list
    auto entries = history_->GetEntries(500);

    // Filter entries
    std::vector<int> filteredIndices;
    std::string filter(historyFilter_);
    // Safe tolower: MSVC debug asserts on char < 0 (non-ASCII)
    auto safeLower = [](char c) -> char { return (char)std::tolower((unsigned char)c); };
    std::transform(filter.begin(), filter.end(), filter.begin(), safeLower);

    for (int i = (int)entries.size() - 1; i >= 0; i--) {
        if (filter.empty()) {
            filteredIndices.push_back(i);
        } else {
            std::string src = entries[i].srcText;
            std::string dst = entries[i].dstText;
            std::transform(src.begin(), src.end(), src.begin(), safeLower);
            std::transform(dst.begin(), dst.end(), dst.begin(), safeLower);
            if (src.find(filter) != std::string::npos ||
                dst.find(filter) != std::string::npos) {
                filteredIndices.push_back(i);
            }
        }
    }

    ImGui::BeginChild("##historyList", ImVec2(-1, -60), true);
    {
        for (int idx : filteredIndices) {
            const auto& entry = entries[idx];

            // Show truncated text
            std::string preview = entry.srcText;
            if (preview.length() > 40)
                preview = preview.substr(0, 40) + "...";

            bool isSelected = (idx == selectedHistoryIndex_);

            // Unique ID per item so same text doesn't share selection state
            ImGui::PushID(idx);
            if (ImGui::Selectable(preview.c_str(), &isSelected)) {
                selectedHistoryIndex_ = (idx == selectedHistoryIndex_) ? -1 : idx;
            }
            ImGui::PopID();
            HandCursor();

            // Show details if selected
            if (isSelected) {
                ImGui::Indent();
                ImGui::TextDisabled("时间: %s", entry.GetTimeString().c_str());
                ImGui::TextDisabled("语言: %s → %s", entry.srcLang.c_str(), entry.dstLang.c_str());
                ImGui::TextWrapped("原文: %s", entry.srcText.c_str());
                ImGui::TextWrapped("译文: %s", entry.dstText.c_str());

                if (ImGui::SmallButton("复制译文")) {
                    ImGui::SetClipboardText(entry.dstText.c_str());
                }
                HandCursor();
                ImGui::SameLine();
                if (ImGui::SmallButton("删除")) {
                    history_->DeleteEntry(idx);
                    selectedHistoryIndex_ = -1;
                }
                HandCursor();
                ImGui::Unindent();
                ImGui::Separator();
            }
        }

        if (filteredIndices.empty()) {
            ImGui::TextDisabled("暂无历史记录");
        }
    }
    ImGui::EndChild();

    // Bottom status
    char countStr[64];
    snprintf(countStr, sizeof(countStr), "共 %d 条记录", history_->GetCount());
    ImGui::TextDisabled("%s", countStr);
}

void UI::RenderSettingsView() {
    // === Fixed header (always visible) ===
    ImGui::TextUnformatted("设置");

    ImGui::Separator();

    // === Scrollable content area ===
    // Compact controls: reduce frame padding for inputs/buttons/combos/sliders
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    {
        float footerH = 40.0f;   // buttons + spacing
        float contentH = max(50.0f, ImGui::GetContentRegionAvail().y - footerH);
        ImGui::BeginChild("##settingsContent", ImVec2(-1, contentH), true,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // Engine status
        ImGui::TextUnformatted("当前引擎");
        ImGui::Separator();
        {
            std::string engineName = translator_->GetActiveEngineName();
            ImGui::Text("翻译引擎: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%s", engineName.c_str());
        }

        ImGui::Spacing();

        // API Configuration section
        ImGui::TextUnformatted("百度翻译 API 配置（可选）");
        ImGui::Separator();

        ImGui::InputTextWithHint("App ID", "留空使用在线翻译（国内可能受限）", settingsAppId_, sizeof(settingsAppId_));
        ImGui::InputTextWithHint("密钥", "留空使用在线翻译（国内可能受限）", settingsApiKey_, sizeof(settingsApiKey_), ImGuiInputTextFlags_Password);

        // Verify API Key button
        if (ImGui::SmallButton("验证 API Key")) {
            if (strlen(settingsAppId_) > 0 && strlen(settingsApiKey_) > 0) {
                apiVerifyDone_ = false;
                translator_->SetCredentials(settingsAppId_, settingsApiKey_);
                auto testResult = translator_->TestBaiduCredentials();

                apiVerifyDone_ = true;
                apiVerifyResult_ = testResult.success;

                if (testResult.success) {
                    SetStatus("API Key 验证成功!");
                } else {
                    SetStatus(std::string("验证失败: ") + testResult.errorMsg, true);
                }
            } else {
                SetStatus("请输入 App ID 和 API Key", true);
            }
        }
        HandCursor();

        ImGui::SameLine();
        if (apiVerifyDone_) {
            ImGui::TextColored(apiVerifyResult_ ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                               apiVerifyResult_ ? "✓ 有效" : "✗ 无效");
        }

        ImGui::Spacing();

        // ── Youdao API Configuration ─────────────────────────────────────
        ImGui::TextUnformatted("有道翻译 API 配置（可选）");
        ImGui::Separator();

        ImGui::InputTextWithHint("应用 ID", "留空使用在线翻译", settingsYoudaoAppId_, sizeof(settingsYoudaoAppId_));
        ImGui::InputTextWithHint("应用密钥", "留空使用在线翻译", settingsYoudaoApiKey_, sizeof(settingsYoudaoApiKey_), ImGuiInputTextFlags_Password);

        // Verify Youdao credentials
        if (ImGui::SmallButton("验证有道密钥")) {
            if (strlen(settingsYoudaoAppId_) > 0 && strlen(settingsYoudaoApiKey_) > 0) {
                youdaoApiVerifyDone_ = false;
                translator_->SetYoudaoCredentials(settingsYoudaoAppId_, settingsYoudaoApiKey_);
                auto testResult = translator_->TestYoudaoCredentials();

                youdaoApiVerifyDone_ = true;
                youdaoApiVerifyResult_ = testResult.success;

                if (testResult.success) {
                    SetStatus("有道密钥验证成功!");
                } else {
                    SetStatus(std::string("验证失败: ") + testResult.errorMsg, true);
                }
            } else {
                SetStatus("请输入有道应用 ID 和密钥", true);
            }
        }
        HandCursor();

        ImGui::SameLine();
        if (youdaoApiVerifyDone_) {
            ImGui::TextColored(youdaoApiVerifyResult_ ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                               youdaoApiVerifyResult_ ? "✓ 有效" : "✗ 无效");
        }

        ImGui::Separator();
        ImGui::Checkbox("开机自启", &settingsAutoStart_);
        HandCursor();

        ImGui::Spacing();
        ImGui::TextUnformatted("翻译设置");
        ImGui::Separator();

        // Default target language (no auto-detect)
        ImGui::Combo("默认目标语言", &settingsTargetLang_, Translator::GetTargetLanguageList(), Translator::GetTargetLanguageCount());

        // Display settings
        ImGui::TextUnformatted("显示设置");
        ImGui::Separator();

        ImGui::SliderInt("窗口透明度", &settingsOpacity_, 50, 255);

        ImGui::EndChild();
    }
    ImGui::PopStyleVar();  // compact FramePadding

    // === Fixed footer: Save/Reset + Back buttons (always visible) ===
    ImGui::Separator();

    if (ImGui::SmallButton("保存设置")) {
        auto& cfg = config_->Get();
        cfg.appId = settingsAppId_;
        cfg.apiKey = settingsApiKey_;
        cfg.youdaoAppId = settingsYoudaoAppId_;
        cfg.youdaoApiKey = settingsYoudaoApiKey_;
        cfg.targetLang = Translator::GetTargetLangCode(settingsTargetLang_);
        cfg.windowOpacity = settingsOpacity_;
        cfg.windowWidth = curW_;
        cfg.windowHeight = curH_;
        cfg.autoStart = settingsAutoStart_;
        config_->Save();

        // Apply settings
        translator_->SetCredentials(cfg.appId, cfg.apiKey);
        translator_->SetYoudaoCredentials(cfg.youdaoAppId, cfg.youdaoApiKey);
        translator_->SetTargetLang(cfg.targetLang);
        toLangIndex_ = settingsTargetLang_;
        if (opacityCallback_) opacityCallback_(cfg.windowOpacity);

        SetStatus("设置已保存");
    }
    HandCursor();

    ImGui::SameLine();
    if (ImGui::SmallButton("恢复默认")) {
        config_->Reset();
        const auto& cfg = config_->Get();
        strcpy_s(settingsAppId_, cfg.appId.c_str());
        strcpy_s(settingsApiKey_, cfg.apiKey.c_str());
        strcpy_s(settingsYoudaoAppId_, cfg.youdaoAppId.c_str());
        strcpy_s(settingsYoudaoApiKey_, cfg.youdaoApiKey.c_str());
        settingsOpacity_ = cfg.windowOpacity;
        settingsAutoStart_ = cfg.autoStart;
        settingsTargetLang_ = Translator::GetTargetLangIndex(cfg.targetLang);
        if (opacityCallback_) opacityCallback_(cfg.windowOpacity);
        // Reset window size immediately
        curW_ = cfg.windowWidth;
        curH_ = cfg.windowHeight;
        if (hwnd_)
            SetWindowPos(hwnd_, nullptr, 0, 0, curW_, curH_, SWP_NOMOVE | SWP_NOZORDER);
        SetStatus("已恢复默认设置");
    }
    HandCursor();

    // Back button on the right, margin matches left side
    float btnW = ImGui::CalcTextSize("返回").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - btnW);
    if (ImGui::SmallButton("返回")) {
        SetViewMode(ViewMode::Main);
    }
    HandCursor();
}

void UI::DoTranslate() {
    std::string text(inputBuf_);

    // Trim whitespace
    text.erase(0, text.find_first_not_of(" \t\n\r"));
    text.erase(text.find_last_not_of(" \t\n\r") + 1);

    if (text.empty()) {
        SetStatus("请输入要翻译的内容", true);
        return;
    }

    strcpy_s(inputBuf_, text.c_str());

    // Configure translator
    const auto& cfg = config_->Get();
    translator_->SetCredentials(cfg.appId, cfg.apiKey);
    translator_->SetYoudaoCredentials(cfg.youdaoAppId, cfg.youdaoApiKey);

    std::string fromLang = Translator::GetLangCode(fromLangIndex_);
    std::string toLang = Translator::GetTargetLangCode(toLangIndex_);

    translator_->SetFromLang(fromLang);
    translator_->SetTargetLang(toLang);

    // Update status
    SetStatus("翻译中...");

    // Perform translation (synchronous)
    auto result = translator_->Translate(text);

    if (result.success) {
        strcpy_s(resultBuf_, result.dstText.c_str());

        // Show detected source language
        strcpy_s(detectedLangBuf_, result.detectedLang.c_str());
        strcpy_s(engineNameBuf_, result.engineName.c_str());
        SetStatus("翻译完成");

        // Save to history
        history_->AddEntry(result.srcText, result.dstText,
                          result.detectedLang, toLang);
    } else {
        SetStatus(result.errorMsg, true);
        resultBuf_[0] = '\0';
        detectedLangBuf_[0] = '\0';
        engineNameBuf_[0] = '\0';
    }
}
