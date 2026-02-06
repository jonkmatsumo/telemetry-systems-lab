#include <gtest/gtest.h>
#include "api_server.h"
#include "http_test_utils.h"
#include "mocks/mock_db_client.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <string>

namespace telemetry {
namespace api {

class ApiServerErrorTest : public ::testing::Test {
protected:
    std::shared_ptr<MockDbClient> mock_db;
    std::unique_ptr<ApiServer> server;
    std::thread server_thread;
    const std::string host = "127.0.0.1";
    int port = 0;

    void SetUp() override {
        port = AllocateTestPort();
        mock_db = std::make_shared<MockDbClient>();
        server = std::make_unique<ApiServer>("localhost:50051", mock_db);
        
        server_thread = std::thread([this]() {
            server->Start(host, port);
        });

        ASSERT_TRUE(WaitForServerReady(host, port))
            << "HTTP API server failed to start on port " << port;
    }

    void TearDown() override {
        if (server) {
            server->Stop();
        }
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
};

TEST_F(ApiServerErrorTest, ReturnsJsonParseError) {
    httplib::Client cli(host, port);
    
    // Invalid JSON
    std::string body = "{ invalid json ";
    
    auto res = cli.Post("/inference", body, "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    
    auto j = nlohmann::json::parse(res->body);
    EXPECT_EQ(j["error"]["code"], "E_HTTP_JSON_PARSE_ERROR");
}

TEST_F(ApiServerErrorTest, ReturnsMissingField) {
    httplib::Client cli(host, port);
    
    // Missing model_run_id (which is required via j.at("model_run_id"))
    nlohmann::json body = {
        {"samples", {}}
    };
    
    auto res = cli.Post("/inference", body.dump(), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    
    auto j = nlohmann::json::parse(res->body);
    EXPECT_EQ(j["error"]["code"], "E_HTTP_MISSING_FIELD");
}

TEST_F(ApiServerErrorTest, ReturnsInvalidArgument) {
    httplib::Client cli(host, port);
    
    // Invalid type for model_run_id (expect string, give int) -> parse error or type error?
    // nlohmann::json throws type_error if at() mismatch? No, at() returns reference.
    // implicit conversion might fail or simple assignment.
    // Let's rely on "samples" missing as typical missing field.
    // For invalid argument, I can trigger my own checks or logic errors.
    // E.g. samples > 1000 throws logic_error? No, I implemented manual check returning E_HTTP_INVALID_ARGUMENT.
    
    nlohmann::json body;
    body["model_run_id"] = "test";
    std::vector<nlohmann::json> samples;
    for(int i=0; i<1001; ++i) samples.push_back({});
    body["samples"] = samples;

    auto res = cli.Post("/inference", body.dump(), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    
    auto j = nlohmann::json::parse(res->body);
    EXPECT_EQ(j["error"]["code"], "E_HTTP_INVALID_ARGUMENT");
}

} // namespace api
} // namespace telemetry
