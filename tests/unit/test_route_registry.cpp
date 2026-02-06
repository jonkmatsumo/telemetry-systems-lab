#include <gtest/gtest.h>
#include "route_registry.h"
#include <set>

namespace telemetry {
namespace api {

TEST(RouteRegistryTest, BasicInvariants) {
    EXPECT_FALSE(kRequiredRoutes.empty());
    
    std::set<std::pair<std::string, std::string>> unique_routes;
    for (const auto& route : kRequiredRoutes) {
        auto res = unique_routes.insert({route.method, route.pattern});
        EXPECT_TRUE(res.second) << "Duplicate route found: " << route.method << " " << route.pattern;
    }
}

TEST(RouteRegistryTest, ExpectedCount) {
    // We identified 35 routes in ApiServer
    EXPECT_EQ(kRequiredRoutes.size(), 35);
}

} // namespace api
} // namespace telemetry
