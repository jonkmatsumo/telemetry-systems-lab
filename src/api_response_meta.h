#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace telemetry::api {

inline auto IsTruncated(int returned, int limit, const std::optional<long>& total_distinct) -> bool {
    if (returned <= 0 || limit <= 0) {
        return false;
    }
    if (total_distinct.has_value()) {
        return *total_distinct > returned;
    }
    return false;
}

struct ResponseMetaArgs {
    int limit;
    int returned;
    bool truncated;
    std::optional<long> total_distinct;
    std::string reason;
    std::optional<int> bins_requested = std::nullopt;
    std::optional<int> bins_returned = std::nullopt;
};

inline auto BuildResponseMeta(const ResponseMetaArgs& args) -> nlohmann::json {
    nlohmann::json meta;
    meta["limit"] = args.limit;
    meta["returned"] = args.returned;
    meta["truncated"] = args.truncated;
    if (args.total_distinct.has_value()) {
        meta["total_distinct"] = *args.total_distinct;
    } else {
        meta["total_distinct"] = nullptr;
    }
    meta["reason"] = args.reason;
    if (args.bins_requested.has_value()) {
        meta["bins_requested"] = *args.bins_requested;
    }
    if (args.bins_returned.has_value()) {
        meta["bins_returned"] = *args.bins_returned;
    }
    return meta;
}

}  // namespace telemetry::api