#pragma once

#include <nlohmann/json.hpp>

inline auto BuildDebugMeta(double duration_ms,
                                     long row_count,
                                     const nlohmann::json& resolved = nlohmann::json::object()) -> nlohmann::json {
    nlohmann::json meta;
    meta["duration_ms"] = duration_ms;
    meta["row_count"] = row_count;
    if (!resolved.empty()) {
        meta["resolved"] = resolved;
    }
    return meta;
}