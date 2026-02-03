#pragma once

#include <optional>

namespace telemetry {
namespace api {

inline bool HasMore(int limit, int offset, int returned, std::optional<long> total) {
    if (total.has_value()) {
        return offset + returned < total.value();
    }
    return returned >= limit;
}

}  // namespace api
}  // namespace telemetry
