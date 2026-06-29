#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

struct HistoryEntry {
    std::string srcText;
    std::string dstText;
    std::string srcLang;
    std::string dstLang;
    std::chrono::system_clock::time_point timestamp;

    std::string GetTimeString() const;
};

class History {
public:
    History();
    ~History();

    void Load(const std::string& filePath);
    void Save();

    void AddEntry(const std::string& srcText, const std::string& dstText,
                  const std::string& srcLang, const std::string& dstLang);
    std::vector<HistoryEntry> GetEntries(int limit = 50) const;
    void Clear();
    void DeleteEntry(size_t index);
    int GetCount() const { return (int)entries_.size(); }
    void SetMaxSize(int max) { maxSize_ = max; }

private:
    std::vector<HistoryEntry> entries_;
    std::string filePath_;
    int maxSize_ = 500;
};
