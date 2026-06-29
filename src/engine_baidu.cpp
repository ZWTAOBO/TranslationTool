#include "engine_baidu.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <cstring>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

void BaiduEngine::SetCredentials(const std::string& appId, const std::string& apiKey) {
    appId_ = appId;
    apiKey_ = apiKey;
}

bool BaiduEngine::Available() const {
    return !appId_.empty() && !apiKey_.empty();
}

std::string BaiduEngine::Name() const {
    return "百度翻译";
}

std::string BaiduEngine::CalculateMD5(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbHash[16];
    DWORD cbHash = 16;
    char hex[33] = { 0 };

    CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash);
    CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.length(), 0);
    CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0);

    for (DWORD i = 0; i < cbHash; i++)
        sprintf_s(hex + i * 2, 3, "%02x", rgbHash[i]);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return std::string(hex);
}

std::string BaiduEngine::MakeRequest(const std::string& url) {
    std::string result;
    HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;

    hSession = WinHttpOpen(L"TranslationTool/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           nullptr, nullptr, 0);

    if (hSession) {
        hConnect = WinHttpConnect(hSession, L"fanyi-api.baidu.com",
                                  INTERNET_DEFAULT_HTTPS_PORT, 0);
    }

    if (hConnect) {
        int urlLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
        std::wstring wurl(urlLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], urlLen);

        hRequest = WinHttpOpenRequest(hConnect, L"GET", wurl.c_str(),
                                       nullptr, nullptr, nullptr,
                                       WINHTTP_FLAG_SECURE);
    }

    if (hRequest) {
        WinHttpSetTimeouts(hRequest, 5000, 5000, 5000, 10000);

        if (WinHttpSendRequest(hRequest, nullptr, 0, nullptr, 0, 0, 0)) {
            if (WinHttpReceiveResponse(hRequest, nullptr)) {
                ReadWinHttpResponse(hRequest, result);
            }
        }

        WinHttpCloseHandle(hRequest);
    }

    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    return result;
}

TranslationResult BaiduEngine::Translate(const std::string& text,
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
        result.errorMsg = "未配置百度翻译 App ID 和密钥";
        return result;
    }

    // Generate salt and sign
    std::string salt = std::to_string(rand());
    std::string signInput = appId_ + text + salt + apiKey_;
    std::string sign = CalculateMD5(signInput);

    // Build URL
    std::string url = "/api/trans/vip/translate?q=" + UrlEncode(text) +
                      "&from=" + from +
                      "&to=" + to +
                      "&appid=" + appId_ +
                      "&salt=" + salt +
                      "&sign=" + sign;

    // Make request
    std::string response = MakeRequest(url);

    if (response.empty()) {
        result.success = false;
        result.errorMsg = "网络连接失败，请检查网络";
        return result;
    }

    // Parse JSON response
    try {
        auto json = nlohmann::json::parse(response);

        // Check for error
        if (json.contains("error_code")) {
            std::string errorCode = json["error_code"].get<std::string>();
            std::string errorMsg = json.contains("error_msg") ? json["error_msg"].get<std::string>() : "";

            result.success = false;
            if (errorCode == "52001") result.errorMsg = "请求超时，请重试";
            else if (errorCode == "52002") result.errorMsg = "系统错误，请重试";
            else if (errorCode == "52003") result.errorMsg = "未授权，请检查 API Key";
            else if (errorCode == "54000") result.errorMsg = "参数错误";
            else if (errorCode == "54001") result.errorMsg = "签名错误";
            else if (errorCode == "54003") result.errorMsg = "访问频率过高，请稍后重试";
            else if (errorCode == "54004") result.errorMsg = "账户余额不足";
            else if (errorCode == "54005") result.errorMsg = "请求频率过高，请降低调用频率";
            else if (errorCode == "58000") result.errorMsg = "客户端 IP 地址错误";
            else if (errorCode == "58001") result.errorMsg = "不支持该语言";
            else if (errorCode == "90107") result.errorMsg = "认证失败，请检查 App ID 和 API Key";
            else result.errorMsg = "翻译失败 (代码: " + errorCode + ")";
            return result;
        }

        // Parse successful response
        if (json.contains("trans_result")) {
            auto& transResult = json["trans_result"];
            if (transResult.is_array() && transResult.size() > 0) {
                std::string full;
                for (size_t i = 0; i < transResult.size(); i++) {
                    if (i > 0) full += "\n";
                    full += transResult[i]["dst"].get<std::string>();
                }
                result.dstText = full;
            }
        }

        if (json.contains("from")) {
            result.detectedLang = json["from"].get<std::string>();
        }

        result.success = true;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.errorMsg = std::string("解析响应失败: ") + e.what();
    }

    return result;
}
