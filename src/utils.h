#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <windows.h>
#include <winhttp.h>

// ── URL-encode a string ──────────────────────────────────────────────────────

inline std::string UrlEncode(const std::string& input) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : input) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            escaped << c;
        else
            escaped << '%' << std::setw(2) << (int)c;
    }
    return escaped.str();
}

// ── Convert wstring to UTF-8 string using WideCharToMultiByte ────────────────
// Unlike std::string(wstr.begin(), wstr.end()), this preserves non-ASCII
// characters and does NOT trigger MSVC debug CRT assertions on narrowing.

inline std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

// ── Read a WinHTTP response body into a string ───────────────────────────────
// Returns false on read error.

inline bool ReadWinHttpResponse(HINTERNET hRequest, std::string& result) {
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            buffer[bytesRead] = '\0';
            result += buffer.data();
        } else {
            return false;
        }
    }
    return true;
}
