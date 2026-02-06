#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "api_server.h"
#include "mocks/mock_db_client.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using ::testing::_;
using ::testing::Return;
// using ::testing::NiceMock; // Not using NiceMock anymore
using ::testing::Invoke;

namespace telemetry {
namespace api {

class ApiServerPerformanceTest : public ::testing::Test {
protected:
    std::unique_ptr<ApiServer> server;
    int port = 50097; // Different port

    void SetUp() override {
        // Defer server creation to test body
    }

    void TearDown() override {
        if (server) server->Stop();
    }
};

TEST_F(ApiServerPerformanceTest, ListModelsUsesBulkFetch) {
    httplib::Client cli("localhost", port);
    
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
    std::thread([this]() {
        server->Start("localhost", port);
    }).detach();
    
    int retries = 20;
    while (retries-- > 0) {
        try {
            httplib::Client cli("localhost", port);
            if (auto res = cli.Get("/health")) break;
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

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
