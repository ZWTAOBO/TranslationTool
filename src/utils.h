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
