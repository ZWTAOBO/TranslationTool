// Include full Windows + UIA BEFORE WIN32_LEAN_AND_MEAN (defined in clipboard.h)
// so UIAutomation.h has access to RPC/COM macros (MIDL_INTERFACE etc.)
#include <windows.h>
#include <UIAutomation.h>

#include "clipboard.h"
#include <iostream>

Clipboard::Clipboard() : stateSaved_(false) {}
Clipboard::~Clipboard() = default;

std::wstring Clipboard::ReadText()
{
    std::wstring result;

    if (!OpenClipboard(nullptr))
    {
        // Retry after short sleep
        Sleep(50);
        if (!OpenClipboard(nullptr))
            return result;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData)
    {
        wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
        if (pText)
        {
            result = pText;
            GlobalUnlock(hData);
        }
    }

    CloseClipboard();
    return result;
}

bool Clipboard::WriteText(const std::wstring& text)
{
    if (!OpenClipboard(nullptr))
    {
        Sleep(50);
        if (!OpenClipboard(nullptr))
            return false;
    }

    EmptyClipboard();

    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HANDLE hData = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hData)
    {
        CloseClipboard();
        return false;
    }

    wchar_t* pData = static_cast<wchar_t*>(GlobalLock(hData));
    if (pData)
    {
        wcscpy_s(pData, text.length() + 1, text.c_str());
        GlobalUnlock(hData);
    }

    SetClipboardData(CF_UNICODETEXT, hData);
    CloseClipboard();
    return true;
}

void Clipboard::SaveState()
{
    if (!OpenClipboard(nullptr))
    {
        Sleep(50);
        if (!OpenClipboard(nullptr))
            return;
    }

    savedState_.clear();

    UINT format = EnumClipboardFormats(0);
    while (format)
    {
        HANDLE hData = GetClipboardData(format);
        if (hData)
        {
            SIZE_T dataSize = GlobalSize(hData);
            void* pData = GlobalLock(hData);
            if (pData && dataSize > 0)
            {
                ClipboardData cd;
                cd.clipFormat = format;
                cd.data.resize(dataSize);
                memcpy(cd.data.data(), pData, dataSize);
                savedState_.push_back(std::move(cd));
                GlobalUnlock(hData);
            }
        }
        format = EnumClipboardFormats(format);
    }

    CloseClipboard();
    stateSaved_ = true;
}

void Clipboard::RestoreState()
{
    if (!stateSaved_)
        return;

    if (!OpenClipboard(nullptr))
    {
        Sleep(50);
        if (!OpenClipboard(nullptr))
            return;
    }

    EmptyClipboard();

    for (const auto& cd : savedState_)
    {
        HANDLE hData = GlobalAlloc(GMEM_MOVEABLE, cd.data.size());
        if (hData)
        {
            void* pData = GlobalLock(hData);
            if (pData)
            {
                memcpy(pData, cd.data.data(), cd.data.size());
                GlobalUnlock(hData);
            }
            SetClipboardData(cd.clipFormat, hData);
        }
    }

    CloseClipboard();
    stateSaved_ = false;
}

bool Clipboard::SimulateCtrlC()
{
    // Our hotkey is Ctrl+Alt+T — the Ctrl key is likely still physically
    // held when this runs.  Sending a fake Ctrl-up would confuse the OS
    // keyboard state (Chrome/Electron apps are sensitive to this).
    // Only send modifier events when Ctrl is NOT already held.
    bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    INPUT inputs[4] = {};
    int count = 0;

    if (!ctrlHeld) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        count++;
    }

    // C down
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = 'C';
    count++;

    // C up
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = 'C';
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    count++;

    if (!ctrlHeld) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    SendInput(count, inputs, sizeof(INPUT));

    // Pump messages so the injected input gets dispatched NOW,
    // while the original foreground window is still active.
    // Without this, the keystrokes sit in the queue until our
    // caller returns to the main message loop — by which point
    // ShowWindow/SetForegroundWindow often fires first, and the
    // Ctrl+C goes to our own window instead of the target app.
    MSG msg;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 100)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(1);
    }
    return true;
}

// Try to read selected text from the focused control directly,
// using EM_GETSEL + WM_GETTEXT — works cross-process, zero clipboard modification.
// Supports standard EDIT / RichEdit controls.
std::wstring Clipboard::TryGetSelectedTextDirect(HWND hwndFocus)
{
    if (!hwndFocus)
        return {};

    // Get selection range via EM_GETSEL (works cross-process)
    DWORD_PTR sel = 0;
    LRESULT ok = SendMessageTimeout(hwndFocus, EM_GETSEL, 0, 0,
                                    SMTO_ABORTIFHUNG, 500, &sel);
    if (!ok)
        return {};

    int start = LOWORD(sel);
    int end   = HIWORD(sel);
    if (start >= end)
        return {};                     // nothing selected

    // Get total text length via WM_GETTEXTLENGTH (works cross-process)
    DWORD_PTR totalLen = 0;
    ok = SendMessageTimeout(hwndFocus, WM_GETTEXTLENGTH, 0, 0,
                            SMTO_ABORTIFHUNG, 500, &totalLen);
    if (!ok || totalLen == 0)
        return {};
    if (totalLen > 65536)
        return {};                     // too large, skip direct read

    // Get full text via WM_GETTEXT (works cross-process, Windows marshals the pointer)
    std::wstring fullText(static_cast<size_t>(totalLen) + 1, L'\0');
    ok = SendMessageTimeout(hwndFocus, WM_GETTEXT, totalLen + 1,
                            reinterpret_cast<LPARAM>(&fullText[0]),
                            SMTO_ABORTIFHUNG, 500, nullptr);
    if (!ok)
        return {};

    // Truncate to actual string length
    fullText.resize(wcslen(fullText.c_str()));

    // Extract the selected portion
    if (start >= static_cast<int>(fullText.length()) ||
        end   >  static_cast<int>(fullText.length()))
        return {};

    return fullText.substr(start, end - start);
}

// Try to read selected text via Windows UI Automation (UIA).
// Works for browsers (Chrome/Edge), VS Code (Electron), and other
// modern apps that implement the UIA TextPattern — no clipboard touch.
std::wstring Clipboard::TryGetSelectedTextUIA(HWND /*hwndFocus*/)
{
    std::wstring result;

    IUIAutomation* pAutomation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                  reinterpret_cast<void**>(&pAutomation));
    if (FAILED(hr) || !pAutomation)
        return result;

    // Get the focused UI element
    IUIAutomationElement* pFocused = nullptr;
    hr = pAutomation->GetFocusedElement(&pFocused);
    if (FAILED(hr) || !pFocused)
    {
        pAutomation->Release();
        return result;
    }

    // Try to get TextPattern from the focused element
    IUIAutomationTextPattern* pTextPattern = nullptr;
    hr = pFocused->GetCurrentPattern(UIA_TextPatternId,
                                     reinterpret_cast<IUnknown**>(&pTextPattern));
    if (FAILED(hr) || !pTextPattern)
    {
        pFocused->Release();
        pAutomation->Release();
        return result;
    }

    // Get selection ranges
    IUIAutomationTextRangeArray* pRanges = nullptr;
    hr = pTextPattern->GetSelection(&pRanges);
    if (SUCCEEDED(hr) && pRanges)
    {
        int count = 0;
        pRanges->get_Length(&count);
        if (count > 0)
        {
            IUIAutomationTextRange* pRange = nullptr;
            pRanges->GetElement(0, &pRange);
            if (pRange)
            {
                BSTR bstrText = nullptr;
                pRange->GetText(-1, &bstrText);
                if (bstrText)
                {
                    result = bstrText;
                    SysFreeString(bstrText);
                }
                pRange->Release();
            }
        }
        pRanges->Release();
    }

    pTextPattern->Release();
    pFocused->Release();
    pAutomation->Release();
    return result;
}

// Try to copy selected text by sending WM_COPY directly to the focused control.
// This avoids keyboard modifier state issues entirely — it's just a message.
// Many controls (EDIT, RichEdit, browser inputs, VS Code editor) handle WM_COPY.
bool Clipboard::TryCopyViaWmCopy(HWND hwndFocus)
{
    if (!hwndFocus)
        return false;

    DWORD seqBefore = GetClipboardSequenceNumber();
    LRESULT sent = SendMessageTimeout(hwndFocus, WM_COPY, 0, 0,
                                      SMTO_ABORTIFHUNG, 500, nullptr);
    if (!sent)
        return false;

    DWORD deadline = GetTickCount() + 500;
    while (GetTickCount() < deadline)
    {
        if (GetClipboardSequenceNumber() != seqBefore)
            return true;
        Sleep(10);
    }
    return false;
}

std::wstring Clipboard::GetSelectedText()
{
    // ── Get handle to focused control ──
    HWND hwndFocus = nullptr;
    {
        HWND fg = GetForegroundWindow();
        if (fg)
        {
            DWORD tid = GetWindowThreadProcessId(fg, nullptr);
            // Attach our input queue to the foreground window's thread so
            // GetFocus() returns THEIR focused control (not ours).
            // This is the classic, reliable cross-process approach.
            AttachThreadInput(GetCurrentThreadId(), tid, TRUE);
            hwndFocus = GetFocus();
            AttachThreadInput(GetCurrentThreadId(), tid, FALSE);
        }
    }

    // ── Method 1: direct read from edit control (no clipboard touch) ──
    if (hwndFocus)
    {
        std::wstring text = TryGetSelectedTextDirect(hwndFocus);
        if (!text.empty())
            return text;
    }

    // ── Method 2: Windows UI Automation (no clipboard touch) ──
    // Works for browsers, Electron apps (VS Code), and other modern UIs.
    if (hwndFocus)
    {
        std::wstring text = TryGetSelectedTextUIA(hwndFocus);
        if (!text.empty())
            return text;
    }

    // ── Method 2.5: WM_COPY message (no keyboard state issues) ──
    // Try this before keyboard simulation — it avoids modifier key conflicts.
    if (hwndFocus)
    {
        SaveState();
        bool copied = TryCopyViaWmCopy(hwndFocus);
        if (copied)
        {
            std::wstring text = ReadText();
            RestoreState();
            if (!text.empty())
                return text;
        }
        else
        {
            RestoreState();
        }
    }

    // ── Method 3: fallback – clipboard via simulated Ctrl+C ──
    SaveState();
    DWORD seqBefore = GetClipboardSequenceNumber();
    SimulateCtrlC();
    // Poll until clipboard actually changes (up to ~500ms).
    // Pump messages while waiting so any pending injected input
    // (Ctrl+C from SimulateCtrlC) gets dispatched to the target app.
    {
        MSG msg;
        DWORD deadline = GetTickCount() + 500;
        while (GetTickCount() < deadline)
        {
            if (GetClipboardSequenceNumber() != seqBefore)
                break;
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                Sleep(10);
            }
        }
    }
    std::wstring text = ReadText();
    RestoreState();
    return text;
}
