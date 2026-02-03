#pragma once

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>

namespace telemetry {
namespace api {

inline std::optional<std::chrono::system_clock::time_point> ParseIsoTime(const std::string& iso) {
    if (iso.empty()) return std::nullopt;
    std::string trimmed = iso;
    if (!trimmed.empty() && trimmed.back() == 'Z') {
        trimmed.pop_back();
    }
    std::tm tm{};
    std::istringstream ss(trimmed);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        return std::nullopt;
    }
#if defined(_WIN32)
    std::time_t tt = _mkgmtime(&tm);
#else
    std::time_t tt = timegm(&tm);
#endif
    return std::chrono::system_clock::from_time_t(tt);
}

inline int SelectBucketSeconds(const std::string& start_time, const std::string& end_time) {
    auto start = ParseIsoTime(start_time);
    auto end = ParseIsoTime(end_time);
    if (!start.has_value() || !end.has_value()) {
        return 3600;
    }
    auto delta = std::chrono::duration_cast<std::chrono::seconds>(*end - *start);
    auto seconds = delta.count();
    if (seconds <= 6 * 3600) return 300;       // 5m
    if (seconds <= 2 * 86400) return 3600;     // 1h
    if (seconds <= 30 * 86400) return 21600;   // 6h
    if (seconds <= 180 * 86400) return 86400;  // 1d
    return 604800;                             // 7d
}

inline std::string BucketLabel(int bucket_seconds) {
    if (bucket_seconds == 300) return "5m";
    if (bucket_seconds == 3600) return "1h";
    if (bucket_seconds == 21600) return "6h";
    if (bucket_seconds == 86400) return "1d";
    if (bucket_seconds == 604800) return "7d";
    return std::to_string(bucket_seconds) + "s";
}

}  // namespace api
}  // namespace telemetry
