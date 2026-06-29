#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

class Clipboard {
public:
    Clipboard();
    ~Clipboard();

    // Read current clipboard text
    std::wstring ReadText();

    // Write text to clipboard
    bool WriteText(const std::wstring& text);

    // Save current clipboard state (all formats)
    void SaveState();

    // Restore clipboard state
    void RestoreState();

    // Simulate Ctrl+C to copy selected text
    bool SimulateCtrlC();

    // Get selected text: save state -> simulate Ctrl+C -> read -> restore
    std::wstring GetSelectedText();

private:
    // Try to read selected text directly from focused window's edit control
    // via EM_GETSEL + EM_GETTEXTRANGE — no clipboard modification
    std::wstring TryGetSelectedTextDirect(HWND hwndFocus);

    // Try to read selected text via Windows UI Automation (UIA).
    // Works for browsers, VS Code, and other modern apps.
    std::wstring TryGetSelectedTextUIA(HWND hwndFocus);

    // Try to copy selected text via WM_COPY message (no keyboard state issues).
    // Returns true if clipboard changed.
    bool TryCopyViaWmCopy(HWND hwndFocus);

    struct ClipboardData {
        std::vector<uint8_t> data;
        UINT clipFormat;
    };
    std::vector<ClipboardData> savedState_;
    bool stateSaved_;
};
