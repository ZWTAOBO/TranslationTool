#include "engine_youdao.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <vector>
#include <cstring>
#include <ctime>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

void YoudaoEngine::SetCredentials(const std::string& appId, const std::string& apiKey) {
    appId_ = appId;
    apiKey_ = apiKey;
}

bool YoudaoEngine::Available() const {
    return !appId_.empty() && !apiKey_.empty();
}

std::string YoudaoEngine::Name() const {
    return "有道翻译";
}

// ── SHA256 via Windows CryptoAPI ────────────────────────────────────────────

std::string YoudaoEngine::CalculateSHA256(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbHash[32];
    DWORD cbHash = 32;
    char hex[65] = {0};

    CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    if (!hProv) return {};

    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);
    if (hHash) {
        CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.length(), 0);
        CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0);

        for (DWORD i = 0; i < cbHash; i++)
            sprintf_s(hex + i * 2, 3, "%02x", rgbHash[i]);

        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);

    return std::string(hex);
}

// ── URL encoding ────────────────────────────────────────────────────────────

// ── POST request helper ─────────────────────────────────────────────────────

std::string YoudaoEngine::MakePostRequest(const std::string& host,
                                           const std::string& path,
                                           const std::string& formBody) {
    std::string result;
    HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;

    hSession = WinHttpOpen(L"YoudaoEngine/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           nullptr, nullptr, 0);
    if (!hSession) return {};

    std::wstring whost(host.begin(), host.end());
    hConnect = WinHttpConnect(hSession, whost.c_str(),
                              INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return {};
    }

    std::wstring wpath(path.begin(), path.end());
    hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                                   nullptr, nullptr, nullptr,
                                   WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    WinHttpSetTimeouts(hRequest, 5000, 5000, 5000, 10000);

    // Enable TLS 1.2
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &protocols, sizeof(protocols));

    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";

    if (WinHttpSendRequest(hRequest, headers, wcslen(headers),
                           (LPVOID)formBody.data(), (DWORD)formBody.length(),
                           (DWORD)formBody.length(), 0))
    {
        if (WinHttpReceiveResponse(hRequest, nullptr)) {
            ReadWinHttpResponse(hRequest, result);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

// ── Language code mapping: app codes (Baidu-style) → Youdao codes ──────────

std::string YoudaoEngine::LangCodeToYoudao(const std::string& code) {
    if (code == "zh")    return "zh-CHS";
    if (code == "jp")    return "ja";
    if (code == "kor")   return "ko";
    if (code == "fra")   return "fr";
    if (code == "de")    return "de";
    if (code == "spa")   return "es";
    if (code == "pt")    return "pt";
    if (code == "ru")    return "ru";
    if (code == "ara")   return "ar";
    if (code == "it")    return "it";
    if (code == "nl")    return "nl";
    if (code == "th")    return "th";
    if (code == "vie")   return "vi";
    if (code == "pl")    return "pl";
    if (code == "el")    return "el";
    if (code == "swe")   return "sv";
    if (code == "en")    return "en";
    // auto-detect or unknown → pass through (Youdao supports "auto")
    return code;
}

// ── Core translate ──────────────────────────────────────────────────────────

TranslationResult YoudaoEngine::Translate(const std::string& text,
                                           const std::string& from,
                                           const std::string& to) {
    TranslationResult result;
    result.srcText = text;

    if (text.empty()) {
        result.success = false;
        result.errorMsg = "请输入要翻译的内容";
        return result;
    }

    if (!Available()) {
        result.success = false;
        result.errorMsg = "未配置有道翻译 App ID 和密钥";
        return result;
    }

    // Convert language codes
    std::string yFrom = LangCodeToYoudao(from);
    std::string yTo = LangCodeToYoudao(to);

    // Generate salt (use random + timestamp for uniqueness)
    { static bool seeded = false; if (!seeded) { srand((unsigned int)time(nullptr)); seeded = true; } }
    std::string salt = std::to_string(rand()) + std::to_string(time(nullptr));

    // curtime = UTC timestamp in seconds
    std::string curtime = std::to_string(time(nullptr));

    // Calculate input for sign:
    // q ≤ 20  → input = q
    // q > 20  → input = q[0:10] + len(q) + q[-10:]
    std::string input;
    if (text.length() <= 20) {
        input = text;
    } else {
        input = text.substr(0, 10) +
                std::to_string(text.length()) +
                text.substr(text.length() - 10);
    }

    // sign = SHA256(appKey + input + salt + curtime + secretKey)
    std::string signInput = appId_ + input + salt + curtime + apiKey_;
    std::string sign = CalculateSHA256(signInput);

    if (sign.empty()) {
        result.success = false;
        result.errorMsg = "签名计算失败 (SHA256)";
        return result;
    }

    // Build form body
    std::string formBody = "q=" + UrlEncode(text) +
                           "&from=" + yFrom +
                           "&to=" + yTo +
                           "&appKey=" + appId_ +
                           "&salt=" + salt +
                           "&sign=" + sign +
                           "&signType=v3" +
                           "&curtime=" + curtime;

    // Make POST request
    std::string response = MakePostRequest("openapi.youdao.com", "/api", formBody);

    if (response.empty()) {
        result.success = false;
        result.errorMsg = "网络连接失败，请检查网络";
        return result;
    }

    // Parse JSON response
    try {
        auto json = nlohmann::json::parse(response);

        // Check error code
        if (json.contains("errorCode")) {
            std::string errorCode = json["errorCode"].get<std::string>();

            if (errorCode != "0") {
                result.success = false;
                if (errorCode == "101")  result.errorMsg = "缺少必填参数";
                else if (errorCode == "102") result.errorMsg = "不支持的语言类型";
                else if (errorCode == "103") result.errorMsg = "翻译文本过长";
                else if (errorCode == "108") result.errorMsg = "应用 ID 无效";
                else if (errorCode == "110") result.errorMsg = "无相关服务的有效应用";
                else if (errorCode == "111") result.errorMsg = "开发者账号无效";
                else if (errorCode == "113") result.errorMsg = "翻译内容不能为空";
                else if (errorCode == "201") result.errorMsg = "解密失败";
                else if (errorCode == "202") result.errorMsg = "签名检验失败";
                else if (errorCode == "203") result.errorMsg = "IP 地址不在访问列表中";
                else if (errorCode == "206") result.errorMsg = "时间戳无效导致签名校验失败";
                else if (errorCode == "207") result.errorMsg = "重放请求";
                else if (errorCode == "302") result.errorMsg = "翻译查询失败";
                else if (errorCode == "303") result.errorMsg = "服务端异常";
                else if (errorCode == "304") result.errorMsg = "翻译失败";
                else if (errorCode == "308") result.errorMsg = "rejectFallback 参数错误";
                else if (errorCode == "309") result.errorMsg = "domain 参数错误";
                else if (errorCode == "310") result.errorMsg = "未开通领域翻译服务";
                else if (errorCode == "401") result.errorMsg = "账户欠费，请充值";
                else if (errorCode == "411" || errorCode == "412") result.errorMsg = "访问频率受限，请稍后重试";
                else result.errorMsg = "翻译失败 (错误码: " + errorCode + ")";
                return result;
            }
        }

        // Parse successful response
        if (json.contains("translation") && json["translation"].is_array()) {
            auto& transArray = json["translation"];
            std::string full;
            for (size_t i = 0; i < transArray.size(); i++) {
                if (i > 0) full += "\n";
                full += transArray[i].get<std::string>();
            }
            result.dstText = full;
        }

        if (json.contains("l")) {
            std::string l = json["l"].get<std::string>();
            // Parse detected language from "EN2zh-CHS" format
            size_t pos = l.find('2');
            if (pos != std::string::npos && pos > 0) {
                result.detectedLang = l.substr(0, pos);
                // Map back to app code
                if (result.detectedLang == "zh-CHS") result.detectedLang = "zh";
                else if (result.detectedLang == "ja") result.detectedLang = "jp";
                else if (result.detectedLang == "ko") result.detectedLang = "kor";
                else if (result.detectedLang == "fr") result.detectedLang = "fra";
                else if (result.detectedLang == "de") result.detectedLang = "de";
                else if (result.detectedLang == "es") result.detectedLang = "spa";
                else if (result.detectedLang == "pt") result.detectedLang = "pt";
                else if (result.detectedLang == "ru") result.detectedLang = "ru";
                else if (result.detectedLang == "ar") result.detectedLang = "ara";
                else if (result.detectedLang == "it") result.detectedLang = "it";
                else if (result.detectedLang == "nl") result.detectedLang = "nl";
                else if (result.detectedLang == "th") result.detectedLang = "th";
                else if (result.detectedLang == "vi") result.detectedLang = "vie";
                else if (result.detectedLang == "pl") result.detectedLang = "pl";
                else if (result.detectedLang == "el") result.detectedLang = "el";
                else if (result.detectedLang == "sv") result.detectedLang = "swe";
            }
        }

        result.success = !result.dstText.empty();
        if (!result.success) {
            result.errorMsg = "翻译结果为空";
        }
    }
    catch (const std::exception& e) {
        result.success = false;
        result.errorMsg = std::string("解析响应失败: ") + e.what();
    }

    return result;
}
