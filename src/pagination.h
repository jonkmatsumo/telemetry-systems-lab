#pragma once

#include <optional>

namespace telemetry::api {

inline auto HasMore(int limit, int offset, int returned, std::optional<long> total) -> bool { // NOLINT(bugprone-easily-swappable-parameters)
    if (total.has_value()) {
        return offset + returned < total.value();
    }
    return returned >= limit;
}

}  // namespace telemetry::api