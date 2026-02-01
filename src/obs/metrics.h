#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../metrics.h"
#include "obs/logging.h"

namespace telemetry {
namespace obs {

inline void EmitCounter(const std::string& name,
                        long value,
                        const std::string& unit,
                        const std::string& component,
                        const std::map<std::string, std::string>& labels = {},
                        const nlohmann::json& fields = nlohmann::json::object()) {
    ::telemetry::metrics::MetricsRegistry::Instance().Increment(name, labels, value);
    nlohmann::json payload = fields;
    payload["metric_name"] = name;
    payload["value"] = value;
    payload["unit"] = unit;
    if (!labels.empty()) {
        nlohmann::json l = nlohmann::json::object();
        for (const auto& kv : labels) {
            l[kv.first] = kv.second;
        }
        payload["labels"] = l;
    }
    LogEvent(LogLevel::Info, "metric", component, payload);
}

inline void EmitHistogram(const std::string& name,
                          double value,
                          const std::string& unit,
                          const std::string& component,
                          const std::map<std::string, std::string>& labels = {},
                          const nlohmann::json& fields = nlohmann::json::object()) {
    ::telemetry::metrics::MetricsRegistry::Instance().RecordLatency(name, labels, value);
    nlohmann::json payload = fields;
    payload["metric_name"] = name;
    payload["value"] = value;
    payload["unit"] = unit;
    if (!labels.empty()) {
        nlohmann::json l = nlohmann::json::object();
        for (const auto& kv : labels) {
            l[kv.first] = kv.second;
        }
        payload["labels"] = l;
    }
    LogEvent(LogLevel::Info, "metric", component, payload);
}

} // namespace obs
} // namespace telemetry
