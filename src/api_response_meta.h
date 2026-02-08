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

inline auto BuildResponseMeta(int limit,
                                        int returned,
                                        bool truncated,
                                        const std::optional<long>& total_distinct,
                                        const std::string& reason,
                                        std::optional<int> bins_requested = std::nullopt,
                                        std::optional<int> bins_returned = std::nullopt) -> nlohmann::json {
    nlohmann::json meta;
    meta["limit"] = limit;
    meta["returned"] = returned;
    meta["truncated"] = truncated;
    if (total_distinct.has_value()) {
        meta["total_distinct"] = *total_distinct;
    } else {
        meta["total_distinct"] = nullptr;
    }
    meta["reason"] = reason;
    if (bins_requested.has_value()) {
        meta["bins_requested"] = *bins_requested;
    }
    if (bins_returned.has_value()) {
        meta["bins_returned"] = *bins_returned;
    }
    return meta;
}

}  // namespace telemetry::api