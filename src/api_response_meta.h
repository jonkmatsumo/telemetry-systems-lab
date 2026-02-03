#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace telemetry {
namespace api {

inline bool IsTruncated(int returned, int limit, const std::optional<long>& total_distinct) {
    if (returned <= 0 || limit <= 0) {
        return false;
    }
    if (total_distinct.has_value()) {
        return *total_distinct > returned;
    }
    return false;
}

inline nlohmann::json BuildResponseMeta(int limit,
                                        int returned,
                                        bool truncated,
                                        const std::optional<long>& total_distinct,
                                        const std::string& reason) {
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
    return meta;
}

}  // namespace api
}  // namespace telemetry
