#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>

// This test assumes the API server is running on localhost:8080
// or as configured by API_URL env var.
class ApiHealthTest : public ::testing::Test {
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

TEST_F(ApiHealthTest, HealthzReturns200) {
    httplib::Client cli(api_url.c_str());
    auto res = cli.Get("/healthz");
    if (res) {
        EXPECT_EQ(res->status, 200);
        auto j = nlohmann::json::parse(res->body);
        EXPECT_EQ(j["status"], "OK");
    } else {
        // Skip if server not running
        GTEST_SKIP() << "API server not running at " << api_url;
    }
}

TEST_F(ApiHealthTest, ReadyzReturns200Or503) {
    httplib::Client cli(api_url.c_str());
    auto res = cli.Get("/readyz");
    if (res) {
        EXPECT_TRUE(res->status == 200 || res->status == 503);
        auto j = nlohmann::json::parse(res->body);
        if (res->status == 200) {
            EXPECT_EQ(j["status"], "READY");
        } else {
            EXPECT_EQ(j["status"], "UNREADY");
        }
    } else {
        // Skip if server not running
        GTEST_SKIP() << "API server not running at " << api_url;
    }
}

TEST_F(ApiHealthTest, ErrorResponseIncludesRequestIdAndErrorCode) {
    httplib::Client cli(api_url.c_str());
    httplib::Headers headers = {{"X-Request-ID", "test-request-id"}};
    auto res = cli.Get("/models/test/error_distribution", headers);
    if (res) {
        EXPECT_EQ(res->status, 400);
        auto j = nlohmann::json::parse(res->body);
        ASSERT_TRUE(j.contains("error"));
        EXPECT_EQ(j["error"]["code"], "E_HTTP_BAD_REQUEST");
        EXPECT_EQ(j["error"]["request_id"], "test-request-id");
    } else {
        GTEST_SKIP() << "API server not running at " << api_url;
    }
}

TEST_F(ApiHealthTest, SuccessResponseIncludesRequestId) {
    httplib::Client cli(api_url.c_str());
    httplib::Headers headers = {{"X-Request-ID", "test-request-id-2"}};
    auto res = cli.Get("/schema/metrics", headers);
    if (res) {
        EXPECT_EQ(res->status, 200);
        auto j = nlohmann::json::parse(res->body);
        EXPECT_EQ(j["request_id"], "test-request-id-2");
    } else {
        GTEST_SKIP() << "API server not running at " << api_url;
    }
}
