#pragma once

#include <string>
#include <vector>

namespace telemetry::api {

struct RouteSpec {
    std::string method;
    std::string pattern;
    std::string handler_name;
};

extern const std::vector<RouteSpec> kRequiredRoutes;

} // namespace telemetry::api