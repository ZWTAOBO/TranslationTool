#include "engine_bing.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>
#include <cstring>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

// ── Helper: UTF-8 narrow → wide, WITHOUT adding null terminator into the string ──
// MultiByteToWideChar with cbMultiByte=-1 includes the null terminator in the
// returned count. We exclude it so the resulting wstring doesn't contain
// embedded nulls (which break header construction for WinHttpSendRequest).

static std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.length(),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.length(),
                        &result[0], len);
    return result;
}

// ── Helper: GetLastError to readable string (shared logic with engine_windows) ─

static std::string WinHttpErrStr(DWORD err) {
    // Reuse the same set as engine_windows.cpp
    switch (err) {
        case ERROR_WINHTTP_OUT_OF_HANDLES:         return "内存不足";
        case ERROR_WINHTTP_TIMEOUT:                return "连接超时";
        case ERROR_WINHTTP_INTERNAL_ERROR:         return "内部错误";
        case ERROR_WINHTTP_INVALID_URL:            return "无效URL";
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:   return "不支持的协议";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:      return "域名解析失败";
        case ERROR_WINHTTP_CONNECTION_ERROR:       return "连接错误";
        case ERROR_WINHTTP_SECURE_FAILURE:         return "TLS/SSL 安全错误";
        case ERROR_WINHTTP_CANNOT_CONNECT:         return "无法连接服务器";
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR: return "自动代理服务错误";
        default: {
            char buf[64];
            snprintf(buf, sizeof(buf), "WinHTTP错误 %u", err);
            return buf;
        }
    }
}

// ── Constructor / Destructor ─────────────────────────────────────────────────

BingEngine::BingEngine()
    : token_{}
{
    // Create a persistent WinHTTP session so cookies from the GET (translator page)
    // are automatically sent with subsequent POST (ttranslatev3) requests.
    sharedSession_ = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        nullptr, nullptr, 0);
    if (sharedSession_) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
                          WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                          WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                          WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(sharedSession_, WINHTTP_OPTION_SECURE_PROTOCOLS,
                         &protocols, sizeof(protocols));
    }
}

BingEngine::~BingEngine() {
    if (sharedSession_) {
        WinHttpCloseHandle(sharedSession_);
    }
}

// ── GetPage: simple GET request returning raw HTML ───────────────────────────

std::string BingEngine::GetPage(const std::string& url) {
    std::string result;
    if (!sharedSession_) return {};

    HINTERNET hConnect = nullptr, hRequest = nullptr;

    hConnect = WinHttpConnect(sharedSession_, L"cn.bing.com",
                              INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) return {};

    std::wstring wurl = Utf8ToWide(url);

    hRequest = WinHttpOpenRequest(hConnect, L"GET", wurl.c_str(),
                                   nullptr, nullptr, nullptr,
                                   WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return {};
    }

    WinHttpSetTimeouts(hRequest, 10000, 30000, 30000, 30000);

    LPCWSTR headers = L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                      L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n";

    if (WinHttpSendRequest(hRequest, headers, wcslen(headers), nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        ReadWinHttpResponse(hRequest, result);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return result;
}

// ── PostForm: POST with form-urlencoded body ─────────────────────────────────

HttpResponse BingEngine::PostForm(const std::string& host,
                                   const std::string& path,
                                   const std::string& formBody,
                                   const std::string& referer) {
    HttpResponse resp;
    if (!sharedSession_) return resp;

    HINTERNET hConnect = nullptr, hRequest = nullptr;

    std::wstring whost = Utf8ToWide(host);

    hConnect = WinHttpConnect(sharedSession_, whost.c_str(),
                              INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        resp.winHttpError = "连接失败: " + WinHttpErrStr(GetLastError());
        return resp;
    }

    std::wstring wpath = Utf8ToWide(path);

    hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                                   nullptr, nullptr, nullptr,
                                   WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        resp.winHttpError = "OpenRequest失败: " + WinHttpErrStr(GetLastError());
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    WinHttpSetTimeouts(hRequest, 10000, 30000, 30000, 30000);

    // Build headers
    std::wstring wheaders = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (!referer.empty()) {
        std::wstring wref = Utf8ToWide(referer);
        wheaders += L"Referer: " + wref + L"\r\n";
    }

    if (!WinHttpSendRequest(hRequest, wheaders.c_str(), -1L,
                            (LPVOID)formBody.data(), (DWORD)formBody.length(),
                            (DWORD)formBody.length(), 0)) {
        resp.winHttpError = "SendRequest失败: " + WinHttpErrStr(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        resp.winHttpError = "ReceiveResponse失败: " + WinHttpErrStr(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    // Query HTTP status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &statusCodeSize, nullptr);
    resp.statusCode = (int)statusCode;

    // Read response body
    ReadWinHttpResponse(hRequest, resp.body);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
}

// ── String extraction helper (no regex dependency) ───────────────────────────

std::string BingEngine::ExtractBetween(const std::string& html,
                                        const std::string& start,
                                        const std::string& end,
                                        size_t offset) const {
    size_t pos = html.find(start, offset);
    if (pos == std::string::npos) return {};
    pos += start.length();
    size_t endPos = html.find(end, pos);
    if (endPos == std::string::npos) return {};
    return html.substr(pos, endPos - pos);
}

// ── Token management ─────────────────────────────────────────────────────────

bool BingEngine::FetchTokens() {
    std::string html = GetPage("/translator");
    if (html.empty()) return false;

    // Extract IG
    std::string ig = ExtractBetween(html, "IG:\"", "\"");
    if (ig.empty()) return false;

    // Extract IID
    std::string iid = ExtractBetween(html, "data-iid=\"", "\"");
    if (iid.empty()) return false;

    // Extract abuse prevention tokens: params_AbusePreventionHelper = [key,"token",expiry]
    const std::string abuseMarker = "params_AbusePreventionHelper";
    size_t pos = html.find(abuseMarker);
    if (pos == std::string::npos) return false;

    pos = html.find('[', pos);
    if (pos == std::string::npos) return false;
    size_t endPos = html.find(']', pos);
    if (endPos == std::string::npos) return false;

    std::string arrStr = html.substr(pos + 1, endPos - pos - 1);

    // Parse: key,"token",expiry
    size_t comma1 = arrStr.find(',');
    if (comma1 == std::string::npos) return false;

    std::string key = arrStr.substr(0, comma1);

    // Token is between quotes after first comma
    size_t quote1 = arrStr.find('"', comma1);
    if (quote1 == std::string::npos) return false;
    size_t quote2 = arrStr.find('"', quote1 + 1);
    if (quote2 == std::string::npos) return false;
    std::string token = arrStr.substr(quote1 + 1, quote2 - quote1 - 1);

    // Expiry is after the second quote's trailing comma
    size_t comma2 = arrStr.find(',', quote2);
    std::string expiryStr;
    if (comma2 != std::string::npos) {
        expiryStr = arrStr.substr(comma2 + 1);
        // Trim whitespace
        expiryStr.erase(0, expiryStr.find_first_not_of(" \t\r\n"));
    }

    // Store tokens
    token_.ig = ig;
    token_.iid = iid;
    token_.key = key;
    token_.token = token;
    token_.expiryIntervalMs = expiryStr.empty() ? 3600000 : std::stoi(expiryStr);
    token_.fetchTime = std::chrono::steady_clock::now();
    token_.requestCount = 0;

    initialized_ = true;
    return true;
}

bool BingEngine::TokensValid() const {
    if (!initialized_) return false;
    if (token_.ig.empty() || token_.token.empty()) return false;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - token_.fetchTime).count();
    // Refresh with 5-minute margin before actual expiry
    return elapsed < (token_.expiryIntervalMs - 300000);
}

std::string BingEngine::MakeTranslateURL() const {
    // Match npm package format: double && because endpoint ends with &
    return "/ttranslatev3?isVertical=1&&IG=" + token_.ig +
           "&IID=" + token_.iid;
}

std::string BingEngine::MakeFormBody(const std::string& text,
                                      const std::string& from,
                                      const std::string& to) const {
    std::string body = "text=" + UrlEncode(text) +
                       "&token=" + UrlEncode(token_.token) +
                       "&key=" + token_.key +
                       "&to=" + UrlEncode(to) +
                       "&tryFetchingGenderDebiasedTranslations=true";
    // Bing requires fromLang even when set to "auto-detect"
    body = "fromLang=" + UrlEncode(from) + "&" + body;
    return body;
}

// ── Available / Name ─────────────────────────────────────────────────────────

bool BingEngine::Available() const {
    // The engine is always available; it lazily fetches tokens on first use.
    return true;
}

std::string BingEngine::Name() const {
    return "必应翻译 (Bing)";
}

// ── Language code mapping ────────────────────────────────────────────────────
// Maps app-internal codes (Baidu-style) to Bing translator codes.

std::string BingEngine::LangCodeToBing(const std::string& code) {
    if (code == "auto")   return "auto-detect";
    if (code == "zh")     return "zh-Hans";
    if (code == "jp")     return "ja";
    if (code == "kor")    return "ko";
    if (code == "fra")    return "fr";
    if (code == "de")     return "de";
    if (code == "spa")    return "es";
    if (code == "pt")     return "pt";
    if (code == "ru")     return "ru";
    if (code == "ara")    return "ar";
    if (code == "it")     return "it";
    if (code == "nl")     return "nl";
    if (code == "th")     return "th";
    if (code == "vie")    return "vi";
    if (code == "pl")     return "pl";
    if (code == "el")     return "el";
    if (code == "swe")    return "sv";
    // Default: pass through
    return code;
}

std::string BingEngine::LangCodeFromBing(const std::string& bingCode) {
    if (bingCode == "zh-Hans" || bingCode == "zh-Hant") return "zh";
    if (bingCode == "ja")     return "jp";
    if (bingCode == "ko")     return "kor";
    if (bingCode == "fr")     return "fra";
    if (bingCode == "de")     return "de";
    if (bingCode == "es")     return "spa";
    if (bingCode == "pt")     return "pt";
    if (bingCode == "ru")     return "ru";
    if (bingCode == "ar")     return "ara";
    if (bingCode == "it")     return "it";
    if (bingCode == "nl")     return "nl";
    if (bingCode == "th")     return "th";
    if (bingCode == "vi")     return "vie";
    if (bingCode == "pl")     return "pl";
    if (bingCode == "el")     return "el";
    if (bingCode == "sv")     return "swe";
    return bingCode;
}

// ── Core translate ───────────────────────────────────────────────────────────

TranslationResult BingEngine::Translate(const std::string& text,
                                         const std::string& from,
                                         const std::string& to) {
    TranslationResult result;
    result.srcText = text;

    if (text.empty()) {
        result.success = false;
        result.errorMsg = "请输入要翻译的内容";
        return result;
    }

    // Ensure we have valid tokens
    if (!TokensValid()) {
        if (!FetchTokens()) {
            result.success = false;
            result.errorMsg = "无法连接必应翻译服务，请检查网络。"
                              "或在设置中配置百度翻译（免费）";
            return result;
        }
    }
    // Map language codes
    std::string srcLang = LangCodeToBing(from);
    std::string dstLang = LangCodeToBing(to);

    // Bing does not support auto-detect for target language
    if (dstLang == "auto-detect") {
        result.success = false;
        result.errorMsg = "必应翻译不支持目标语言自动检测，请选择具体目标语言";
        return result;
    }

    // For multi-line text, translate each line separately
    std::vector<std::string> lines;
    {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
    }

    std::string fullTranslation;
    std::string detectedLang;
    bool firstLine = true;

    for (const auto& line : lines) {
        if (!firstLine) fullTranslation += "\n";
        firstLine = false;

        if (line.empty()) continue;

        // Build form body and POST
        std::string formBody = MakeFormBody(line, srcLang, dstLang);
        std::string path = MakeTranslateURL();

        auto resp = PostForm("cn.bing.com", path, formBody,
                             "https://cn.bing.com/translator");

        if (resp.statusCode == 0 && !resp.winHttpError.empty()) {
            result.success = false;
            result.errorMsg = "必应翻译连接失败: " + resp.winHttpError;
            return result;
        }
        if (resp.statusCode != 200 || resp.body.empty()) {
            // On 401/403, invalidate tokens so next call will fetch fresh ones
            if (resp.statusCode == 401 || resp.statusCode == 403) {
                initialized_ = false;
            }
            result.success = false;
            // Include response body in error for diagnostics (truncate if too long)
            std::string bodyPreview = resp.body.substr(0, 300);
            result.errorMsg = "必应翻译服务暂不可用 (HTTP " +
                              std::to_string(resp.statusCode) + ") " +
                              bodyPreview;
            return result;
        }

        // Check for captcha response
        if (resp.body.find("ShowCaptcha") != std::string::npos) {
            // Token might be invalidated, force refresh next time
            initialized_ = false;
            result.success = false;
            result.errorMsg = "必应翻译请求过于频繁，请稍后重试";
            return result;
        }

        // Parse JSON response
        try {
            auto doc = nlohmann::json::parse(resp.body);

            // Bing can return either an array [{}] or a bare object {}.
            // Normalize to the array-entry that contains "translations".
            nlohmann::json entry;
            if (doc.is_array() && !doc.empty()) {
                entry = doc[0];
            } else if (doc.contains("translations")) {
                // Object format: {"translations": [...], "detectedLanguage": {...}}
                entry = doc;
            } else {
                result.success = false;
                result.errorMsg = "必应翻译响应格式异常: " + resp.body.substr(0, 200);
                return result;
            }

            if (!entry.contains("translations") ||
                entry["translations"].empty()) {
                result.success = false;
                result.errorMsg = "必应翻译无返回结果";
                return result;
            }

            fullTranslation += entry["translations"][0]["text"]
                                   .get<std::string>();

            // Capture detected language from first line only
            if (detectedLang.empty() && entry.contains("detectedLanguage")) {
                auto& dl = entry["detectedLanguage"];
                if (dl.contains("language")) {
                    detectedLang = LangCodeFromBing(
                        dl["language"].get<std::string>());
                }
            }

            // Increment request counter for this token session
            token_.requestCount++;
        }
        catch (const std::exception& e) {
            result.success = false;
            result.errorMsg = std::string("解析必应翻译响应失败: ") + e.what();
            return result;
        }
    }

    result.dstText = fullTranslation;
    result.detectedLang = (from == "auto" && !detectedLang.empty())
                              ? detectedLang : from;
    result.success = true;
    return result;
}
