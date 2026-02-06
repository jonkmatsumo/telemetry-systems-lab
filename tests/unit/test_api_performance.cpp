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

class ApiServerPerformanceTest : public ::testing::Test {
protected:
    std::unique_ptr<ApiServer> server;
    std::thread server_thread;
    const std::string host = "127.0.0.1";
    int port = 0;

    void SetUp() override {
        port = AllocateTestPort();
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

TEST_F(ApiServerPerformanceTest, ListModelsUsesBulkFetch) {
    httplib::Client cli(host, port);
    
    class TestMockDb : public MockDbClient {
    public:
        nlohmann::json list_models_result;
        nlohmann::json ListModelRuns(int limit,
                                     int offset,
                                     const std::string& status = "",
                                     const std::string& dataset_id = "",
                                     const std::string& created_from = "",
                                     const std::string& created_to = "") override {
            return list_models_result;
        }
    };
    
    auto my_mock = std::make_shared<TestMockDb>();
    // Inject models
    nlohmann::json models_list = nlohmann::json::array();
    for (int i = 0; i < 5; ++i) {
        models_list.push_back({
            {"model_run_id", "run_" + std::to_string(i)},
            {"parent_run_id", nullptr}, // All roots
            {"best_metric_value", 0.1},
            {"best_metric_name", "mae"}
        });
    }
    my_mock->list_models_result = models_list;
    
    // Init server with my_mock
    server = std::make_unique<ApiServer>("localhost:50051", my_mock);
    server_thread = std::thread([this]() {
        server->Start(host, port);
    });

    ASSERT_TRUE(WaitForServerReady(host, port))
        << "HTTP API server failed to start on port " << port;

    auto res = cli.Get("/models");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    
    // Verify optimization:
    // GetBulkHpoTrialSummaries should be called ONCE
    EXPECT_EQ(my_mock->get_bulk_hpo_count, 1);
    // GetHpoTrials should NOT be called
    EXPECT_EQ(my_mock->get_hpo_trials_count, 0);
    
    auto j = nlohmann::json::parse(res->body);
    ASSERT_EQ(j["items"].size(), 5);
    
    // Check if summary was populated form the dummy data in MockDbClient check
    // MockDbClient::GetBulkHpoTrialSummaries returns dummy data for all IDs
    auto item = j["items"][0];
    EXPECT_TRUE(item.contains("hpo_summary"));
    EXPECT_EQ(item["hpo_summary"]["trial_count"], 10);
    EXPECT_EQ(item["status"], "COMPLETED");
}

} // namespace api
} // namespace telemetry
