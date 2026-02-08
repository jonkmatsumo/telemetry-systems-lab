#pragma once

#include <nlohmann/json.hpp>

struct DebugMetaArgs {
    double duration_ms;
    long row_count;
    nlohmann::json resolved = nlohmann::json::object();
};

inline auto BuildDebugMeta(const DebugMetaArgs& args) -> nlohmann::json {
    nlohmann::json meta;
    meta["duration_ms"] = args.duration_ms;
    meta["row_count"] = args.row_count;
    if (!args.resolved.empty()) {
        meta["resolved"] = args.resolved;
    }
    return meta;
}