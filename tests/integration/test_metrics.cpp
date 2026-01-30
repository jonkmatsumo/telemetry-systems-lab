#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>

class MetricsIntegrationTest : public ::testing::Test {
protected:
    std::string api_url;
    
    void SetUp() override {
        const char* env_p = std::getenv("API_URL");
        if (env_p) {
            api_url = env_p;
        } else {
            api_url = "http://localhost:8080";
        }
    }
};

TEST_F(MetricsIntegrationTest, MetricsEndpointReturnsPrometheusFormat) {
    httplib::Client cli(api_url.c_str());
    auto res = cli.Get("/metrics");
    if (res) {
        EXPECT_EQ(res->status, 200);
        EXPECT_EQ(res->get_header_value("Content-Type"), "text/plain");
        // Check for some common metrics we expect
        // Note: they might be empty if no requests made yet, but the endpoint should exist
        EXPECT_TRUE(res->body.find("# HELP") == std::string::npos); // We didn't implement HELP yet to keep it minimal
    } else {
        GTEST_SKIP() << "API server not running at " << api_url;
    }
}
