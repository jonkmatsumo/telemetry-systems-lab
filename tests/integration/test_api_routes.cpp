#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
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
        
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        replace_all(path, "([a-zA-Z0-9-]+)", "00000000-0000-0000-0000-000000000000");
        replace_all(path, "([0-9]+)", "1");
        replace_all(path, "([a-zA-Z0-9_]+)", "cpu_usage");

        if (route.method == "GET") {
            res = client->Get(path.c_str());
        } else if (route.method == "POST") {
            res = client->Post(path.c_str(), "{}", "application/json");
        } else if (route.method == "DELETE") {
            res = client->Delete(path.c_str());
        } else {
            FAIL() << "Unsupported method " << route.method << " for route " << path;
            continue;
        }

        if (*res) {
            if ((*res)->status == 404) {
                // If it's a 404, check if it's a "Resource not found" JSON error vs a "Route not found" 404.
                // Our API returns JSON errors for resource misses.
                try {
                    auto j = nlohmann::json::parse((*res)->body);
                    EXPECT_TRUE(j.contains("error")) << "404 for " << route.method << " " << path << " is not a structured error: " << (*res)->body;
                } catch (...) {
                    FAIL() << "Route " << route.method << " " << path << " returned 404 with non-JSON body: " << (*res)->body;
                }
            }
        } else {
            // If we can't connect, skip instead of failing if not in CI?
            // For now, fail to be strict.
            FAIL() << "Failed to connect to " << api_url << " for route " << path;
        }
    }
}
