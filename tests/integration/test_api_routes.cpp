#include <gtest/gtest.h>
#include <httplib.h>
#include "route_registry.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <optional>

using namespace telemetry::api;

class ApiRouteTest : public ::testing::Test {
protected:
    std::string api_url;
    std::unique_ptr<httplib::Client> client;

    void SetUp() override {
        const char* env_url = std::getenv("API_URL");
        if (env_url) {
            api_url = env_url;
        } else {
            api_url = "http://localhost:8280";
        }
        
        client = std::make_unique<httplib::Client>(api_url.c_str());
        client->set_connection_timeout(2, 0);
        client->set_read_timeout(2, 0);
    }
};

TEST_F(ApiRouteTest, ProbesAllRequiredRoutes) {
    for (const auto& route : kRequiredRoutes) {
        std::optional<httplib::Result> res;
        
        // Use a dummy ID for patterns with groups
        std::string path = route.pattern;
        // Replace regex groups with a simple ID "123" or similar
        // This is a crude replacement just to avoid 404
        size_t pos;
        while ((pos = path.find("([a-zA-Z0-9-]+)")) != std::string::npos) {
            path.replace(pos, 15, "123");
        }
        while ((pos = path.find("([0-9]+)")) != std::string::npos) {
            path.replace(pos, 8, "1");
        }
        while ((pos = path.find("([a-zA-Z0-9_]+)")) != std::string::npos) {
            path.replace(pos, 15, "cpu_usage");
        }

        if (route.method == "GET") {
            res = client->Get(path.c_str());
        } else if (route.method == "POST") {
            res = client->Post(path.c_str(), "{}", "application/json");
        } else {
            FAIL() << "Unsupported method " << route.method << " for route " << path;
            continue;
        }

        if (*res) {
            EXPECT_NE((*res)->status, 404) << "Route " << route.method << " " << path << " returned 404";
        } else {
            // If we can't connect, skip instead of failing if not in CI?
            // For now, fail to be strict.
            FAIL() << "Failed to connect to " << api_url << " for route " << path;
        }
    }
}
