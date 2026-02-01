#pragma once

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace telemetry {
namespace obs {

enum class LogLevel {
    Info,
    Warn,
    Error
};

inline const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

inline std::string NowIso8601() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = time_point_cast<seconds>(now);
    auto ms = duration_cast<milliseconds>(now - secs).count();
    std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << ms << "Z";
    return oss.str();
}

inline void LogEvent(LogLevel level,
                     const std::string& event,
                     const std::string& component,
                     const nlohmann::json& fields = nlohmann::json::object()) {
    nlohmann::json j = fields;
    j["ts"] = NowIso8601();
    j["level"] = LevelToString(level);
    j["event"] = event;
    j["component"] = component;

    switch (level) {
        case LogLevel::Info:
            spdlog::info(j.dump());
            break;
        case LogLevel::Warn:
            spdlog::warn(j.dump());
            break;
        case LogLevel::Error:
            spdlog::error(j.dump());
            break;
    }
}

class ScopedTimer {
public:
    ScopedTimer(std::string event, std::string component, nlohmann::json fields = nlohmann::json::object())
        : event_(std::move(event)),
          component_(std::move(component)),
          fields_(std::move(fields)),
          start_(std::chrono::steady_clock::now()) {}

    void Stop(LogLevel level = LogLevel::Info, nlohmann::json extra = nlohmann::json::object()) {
        if (stopped_) return;
        stopped_ = true;
        auto duration_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
        nlohmann::json payload = fields_;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            payload[it.key()] = it.value();
        }
        payload["duration_ms"] = duration_ms;
        LogEvent(level, event_, component_, payload);
    }

    ~ScopedTimer() {
        if (!stopped_) {
            Stop(LogLevel::Info);
        }
    }

private:
    std::string event_;
    std::string component_;
    nlohmann::json fields_;
    std::chrono::steady_clock::time_point start_;
    bool stopped_ = false;
};

} // namespace obs
} // namespace telemetry
