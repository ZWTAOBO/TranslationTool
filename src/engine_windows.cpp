#include "engine_windows.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <sstream>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

// ── Language code mapping ───────────────────────────────────────────────────
// Maps app-internal codes (Baidu-style) to MyMemory ISO codes.

std::string WindowsEngine::LangCodeToMyMemory(const std::string& code) {
    if (code == "zh")      return "zh-CN";
    if (code == "jp")      return "ja";
    if (code == "kor")     return "ko";
    if (code == "fra")     return "fr";
    if (code == "de")      return "de";
    if (code == "spa")     return "es";
    if (code == "pt")      return "pt";
    if (code == "ru")      return "ru";
    if (code == "ara")     return "ar";
    if (code == "it")      return "it";
    if (code == "nl")      return "nl";
    if (code == "th")      return "th";
    if (code == "vie")     return "vi";
    if (code == "pl")      return "pl";
    if (code == "el")      return "el";
    if (code == "swe")     return "sv";
    // Default: pass through (handles "en", "auto", and unknown codes)
    return code;
}

// ── URL encoding ────────────────────────────────────────────────────────────

// ── Helper: convert WinHTTP error code to readable string ──────────────────

static std::string WinHttpErrorStr(DWORD err) {
    switch (err) {
        case ERROR_WINHTTP_OUT_OF_HANDLES:         return "内存不足";
        case ERROR_WINHTTP_TIMEOUT:                return "连接超时";
        case ERROR_WINHTTP_INTERNAL_ERROR:         return "内部错误";
        case ERROR_WINHTTP_INVALID_URL:            return "无效URL";
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:   return "不支持的协议";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:      return "域名解析失败";
        case ERROR_WINHTTP_INVALID_OPTION:         return "无效选项";
        case ERROR_WINHTTP_OPTION_NOT_SETTABLE:    return "选项不可设置";
        case ERROR_WINHTTP_SHUTDOWN:               return "已关闭";
        case ERROR_WINHTTP_LOGIN_FAILURE:          return "登录失败";
        case ERROR_WINHTTP_OPERATION_CANCELLED:    return "已取消";
        case ERROR_WINHTTP_INCORRECT_HANDLE_STATE: return "句柄状态错误";
        case ERROR_WINHTTP_CONNECTION_ERROR:       return "连接错误";
        case ERROR_WINHTTP_RESEND_REQUEST:         return "需要重发请求";
        case ERROR_WINHTTP_SECURE_FAILURE:         return "TLS/SSL 安全错误";
        case ERROR_WINHTTP_CANNOT_CONNECT:         return "无法连接服务器";
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:return "需要客户端证书";
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR: return "自动代理服务错误";
        default: {
            char buf[64];
            snprintf(buf, sizeof(buf), "WinHTTP错误 %u", err);
            return buf;
        }
    }
}

// ── HTTP GET request ────────────────────────────────────────────────────────

HttpResponse WindowsEngine::MakeRequest(const std::string& url) {
    HttpResponse resp;
    HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
    DWORD lastErr = 0;

    hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                           WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           nullptr, nullptr, 0);

    if (!hSession) {
        resp.winHttpError = "WinHttpOpen失败: " + WinHttpErrorStr(GetLastError());
        return resp;
    }

    // Explicitly enable TLS 1.2 (required by some servers, and
    // some Win10 builds don't enable it by default for WinHTTP)
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &protocols, sizeof(protocols));

    hConnect = WinHttpConnect(hSession, L"api.mymemory.translated.net",
                              INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!hConnect) {
        lastErr = GetLastError();
        resp.winHttpError = "WinHttpConnect失败: " + WinHttpErrorStr(lastErr);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Convert URL path to wide string
    int urlLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wurl(urlLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], urlLen);

    hRequest = WinHttpOpenRequest(hConnect, L"GET", wurl.c_str(),
                                   nullptr, nullptr, nullptr,
                                   WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        lastErr = GetLastError();
        resp.winHttpError = "WinHttpOpenRequest失败: " + WinHttpErrorStr(lastErr);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Increase timeouts for potentially slow connections
    WinHttpSetTimeouts(hRequest, 10000, 30000, 30000, 30000);

    LPCWSTR headers = L"Accept: application/json, text/plain, */*\r\n"
                      L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
                      L"Referer: https://api.mymemory.translated.net/\r\n";

    if (!WinHttpSendRequest(hRequest, headers, wcslen(headers), nullptr, 0, 0, 0)) {
        lastErr = GetLastError();
        resp.winHttpError = "WinHttpSendRequest失败: " + WinHttpErrorStr(lastErr);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        lastErr = GetLastError();
        resp.winHttpError = "WinHttpReceiveResponse失败: " + WinHttpErrorStr(lastErr);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Query HTTP status code (always succeeds after WinHttpReceiveResponse)
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &statusCode, &statusCodeSize, nullptr)) {
        resp.winHttpError = "无法获取HTTP状态码: " + WinHttpErrorStr(GetLastError());
    }
    resp.statusCode = (int)statusCode;

    // Read response body
    ReadWinHttpResponse(hRequest, resp.body);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
}

// ── Line splitting (MyMemory handles single-line better) ─────────────────────

std::vector<std::string> WindowsEngine::SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        // Keep empty lines as placeholders for reassembly
        lines.push_back(line);
    }
    return lines;
}

// ── Core translate ──────────────────────────────────────────────────────────

bool WindowsEngine::Available() const {
    // Always available — no dependencies to check at init time.
    return true;
}

std::string WindowsEngine::Name() const {
    return "在线翻译 (MyMemory)";
}

TranslationResult WindowsEngine::Translate(const std::string& text,
                                            const std::string& from,
                                            const std::string& to) {
    TranslationResult result;
    result.srcText = text;

    if (text.empty()) {
        result.success = false;
        result.errorMsg = "请输入要翻译的内容";
        return result;
    }

    // Map language codes
    std::string srcLang = (from == "auto") ? "en" : LangCodeToMyMemory(from);
    std::string dstLang = LangCodeToMyMemory(to);
    std::string langPair = srcLang + "|" + dstLang;

    // For multi-line text, translate each line separately to avoid truncation
    auto lines = SplitLines(text);
    std::string fullTranslation;
    bool firstLine = true;

    for (const auto& line : lines) {
        if (!firstLine) fullTranslation += "\n";
        firstLine = false;

        if (line.empty()) continue;

        // Build URL
        std::string url = "/get?q=" + UrlEncode(line) +
                          "&langpair=" + UrlEncode(langPair);

        // Make request
        auto resp = MakeRequest(url);

        // Check HTTP-level errors
        if (resp.statusCode == 403) {
            result.success = false;
            result.errorMsg = "在线翻译服务被网络限制（HTTP 403）。"
                              "请在设置中配置百度翻译（免费，每月100万字符）";
            return result;
        }
        if (resp.statusCode == 0 && !resp.winHttpError.empty()) {
            result.success = false;
            result.errorMsg = "网络连接失败: " + resp.winHttpError + "。"
                              "请在设置中配置百度翻译（免费，国内可用）";
            return result;
        }
        if (resp.statusCode == 0 || resp.body.empty()) {
            result.success = false;
            result.errorMsg = "网络连接失败，请检查网络。"
                              "或在设置中配置百度翻译（免费，国内可用）";
            return result;
        }

        // Parse JSON
        try {
            auto json = nlohmann::json::parse(resp.body);

            // responseStatus can be number (200) or string ("200") depending on API version
            std::string statusStr;
            auto& rs = json["responseStatus"];
            if (rs.is_string())
                statusStr = rs.get<std::string>();
            else if (rs.is_number())
                statusStr = std::to_string(rs.get<int>());

            if (statusStr != "200") {
                result.success = false;
                result.errorMsg = "翻译服务暂时不可用 (代码: " + statusStr + ")";
                return result;
            }

            auto& data = json["responseData"];
            fullTranslation += data["translatedText"].get<std::string>();
        }
        catch (const std::exception& e) {
            result.success = false;
            result.errorMsg = std::string("解析响应失败: ") + e.what();
            return result;
        }
    }

    result.dstText = fullTranslation;
    result.detectedLang = (from == "auto") ? srcLang : from;
    result.success = true;
    return result;
}
