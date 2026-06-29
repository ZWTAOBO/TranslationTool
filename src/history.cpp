#include "history.h"
#include <fstream>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

// JSON serialization helpers for time_point
static std::string TimePointToString(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    localtime_s(&tm, &time_t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static std::chrono::system_clock::time_point StringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
}

// Custom JSON converters for HistoryEntry
namespace nlohmann {
    template<>
    struct adl_serializer<HistoryEntry> {
        static void to_json(json& j, const HistoryEntry& e) {
            j = json{
                {"srcText", e.srcText},
                {"dstText", e.dstText},
                {"srcLang", e.srcLang},
                {"dstLang", e.dstLang},
                {"timestamp", TimePointToString(e.timestamp)}
            };
        }

        static void from_json(const json& j, HistoryEntry& e) {
            j.at("srcText").get_to(e.srcText);
            j.at("dstText").get_to(e.dstText);
            j.at("srcLang").get_to(e.srcLang);
            j.at("dstLang").get_to(e.dstLang);
            auto ts = j.at("timestamp").get<std::string>();
            e.timestamp = StringToTimePoint(ts);
        }
    };
}

std::string HistoryEntry::GetTimeString() const {
    return TimePointToString(timestamp);
}

History::History() = default;
History::~History() = default;

void History::Load(const std::string& filePath) {
    filePath_ = filePath;
    std::ifstream file(filePath_);
    if (!file.is_open()) return;

    try {
        nlohmann::json j;
        file >> j;
        entries_ = j.get<std::vector<HistoryEntry>>();
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load history: " << e.what() << std::endl;
        entries_.clear();
    }
}

void History::Save() {
    if (filePath_.empty()) return;
    try {
        // Trim to max size before saving
        while ((int)entries_.size() > maxSize_)
            entries_.erase(entries_.begin());

        nlohmann::json j = entries_;
        std::ofstream file(filePath_);
        file << j.dump(2);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to save history: " << e.what() << std::endl;
    }
}

void History::AddEntry(const std::string& srcText, const std::string& dstText,
                       const std::string& srcLang, const std::string& dstLang) {
    HistoryEntry entry;
    entry.srcText = srcText;
    entry.dstText = dstText;
    entry.srcLang = srcLang;
    entry.dstLang = dstLang;
    entry.timestamp = std::chrono::system_clock::now();

    entries_.push_back(entry);

    // Trim if exceeded
    while ((int)entries_.size() > maxSize_)
        entries_.erase(entries_.begin());

    Save();
}

std::vector<HistoryEntry> History::GetEntries(int limit) const {
    if (limit <= 0 || limit >= (int)entries_.size())
        return entries_;

    // Return most recent entries
    return std::vector<HistoryEntry>(
        entries_.end() - std::min((int)entries_.size(), limit),
        entries_.end()
    );
}

void History::Clear() {
    entries_.clear();
    Save();
}

void History::DeleteEntry(size_t index) {
    if (index < entries_.size()) {
        entries_.erase(entries_.begin() + index);
        Save();
    }
}
