#include "config.h"
#include <fstream>
#include <iostream>
#include <windows.h>

// Apply or remove the auto-start registry entry.
// Called after loading config and after saving config.
void ApplyAutoStart(bool enabled, const std::wstring& exePath)
{
    HKEY hKey;
    if (enabled) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, L"TranslationTool", 0, REG_SZ,
                (const BYTE*)exePath.c_str(),
                (DWORD)((exePath.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"TranslationTool");
            RegCloseKey(hKey);
        }
    }
}

Config::Config()
{
    // Get executable full path (for auto-start registry) and directory (for config)
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    exePath_ = path;
    std::wstring ws(path);
    std::string exeDir(ws.begin(), ws.end());
    size_t pos = exeDir.find_last_of('\\');
    if (pos != std::string::npos)
        exeDir = exeDir.substr(0, pos);

    filePath_ = exeDir + "\\config.json";
}

Config::~Config() = default;

void Config::Load()
{
    std::ifstream file(filePath_);
    if (!file.is_open())
    {
        // File doesn't exist, create default config
        Save();
        return;
    }

    try
    {
        nlohmann::json j;
        file >> j;
        config_ = j.get<AppConfig>();
        ApplyAutoStart(config_.autoStart, exePath_);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        // Back up corrupted config and use defaults
        std::ifstream src(filePath_, std::ios::binary);
        std::ofstream dst(filePath_ + ".bak", std::ios::binary);
        dst << src.rdbuf();
        src.close();
        dst.close();
        Reset();
        Save();
    }
}

void Config::Save()
{
    try
    {
        nlohmann::json j = config_;
        std::ofstream file(filePath_);
        file << j.dump(2);
        ApplyAutoStart(config_.autoStart, exePath_);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to save config: " << e.what() << std::endl;
    }
}

void Config::Reset()
{
    config_ = AppConfig{};
}
