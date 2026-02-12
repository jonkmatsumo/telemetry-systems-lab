#pragma once

#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "obs/metrics.h"

namespace telemetry::db {

inline auto ParseJsonPayloadWithObservability(const std::string& raw_json, // NOLINT(bugprone-easily-swappable-parameters)
                                              const std::string& field_name,
                                              const std::string& record_id,
                                              const std::string& operation,
                                              const nlohmann::json& parse_failure_value) -> nlohmann::json {
    try {
        return nlohmann::json::parse(raw_json);
    } catch (const std::exception& e) {
        telemetry::obs::EmitCounter("db_json_parse_failures_total", 1, "count", "db",
                                    {{"operation", operation}, {"field", field_name}});
        spdlog::warn("JSON parse failed in {} for field '{}' record_id '{}': {}",
                     operation, field_name, record_id, e.what());
        return parse_failure_value;
    }
}

} // namespace telemetry::db
