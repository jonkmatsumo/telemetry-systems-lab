#pragma once

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>

namespace telemetry::api {

inline auto ParseIsoTime(const std::string& iso) -> std::optional<std::chrono::system_clock::time_point> {
    if (iso.empty()) { return std::nullopt; }
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

inline auto FormatIsoTime(std::chrono::system_clock::time_point tp) -> std::string {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

inline auto PreviousPeriodWindow(
    const std::string& start_time,
    const std::string& end_time) -> std::optional<std::pair<std::string, std::string>> {
    auto start = ParseIsoTime(start_time);
    auto end = ParseIsoTime(end_time);
    if (!start.has_value() || !end.has_value()) {
        return std::nullopt;
    }
    if (*end <= *start) {
        return std::nullopt;
    }
    auto duration = *end - *start;
    auto baseline_end = *start;
    auto baseline_start = baseline_end - duration;
    return std::make_pair(FormatIsoTime(baseline_start), FormatIsoTime(baseline_end));
}

inline auto SelectBucketSeconds(const std::string& start_time, const std::string& end_time) -> int {
    auto start = ParseIsoTime(start_time);
    auto end = ParseIsoTime(end_time);
    if (!start.has_value() || !end.has_value()) {
        return 3600;
    }
    auto delta = std::chrono::duration_cast<std::chrono::seconds>(*end - *start);
    auto seconds = delta.count();
    if (seconds <= 6L * 3600L) { return 300; }       // 5m
    if (seconds <= 2L * 86400L) { return 3600; }     // 1h
    if (seconds <= 30L * 86400L) { return 21600; }   // 6h
    if (seconds <= 180L * 86400L) { return 86400; }  // 1d
    return 604800;                             // 7d
}

inline auto BucketLabel(int bucket_seconds) -> std::string {
    if (bucket_seconds == 300) { return "5m"; }
    if (bucket_seconds == 3600) { return "1h"; }
    if (bucket_seconds == 21600) { return "6h"; }
    if (bucket_seconds == 86400) { return "1d"; }
    if (bucket_seconds == 604800) { return "7d"; }
    return std::to_string(bucket_seconds) + "s";
}

}  // namespace telemetry::api