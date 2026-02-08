#include <gtest/gtest.h>
#include "api_server.h"
#include "mocks/mock_db_client.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

#ifndef TELEMETRY_SOURCE_DIR
#define TELEMETRY_SOURCE_DIR "."
#endif

namespace telemetry::api {

class ApiServerTestPeer {
public:
    static void HandleScoreDatasetJob(ApiServer& server, const httplib::Request& req, httplib::Response& res) {
        server.HandleScoreDatasetJob(req, res);
    }
};

class ApiScoringTest : public ::testing::Test {
protected:
    std::shared_ptr<MockDbClient> mock_db;
    std::unique_ptr<ApiServer> server;

    void SetUp() override {
        mock_db = std::make_shared<MockDbClient>();
        mock_db->mock_artifact_path =
            std::string(TELEMETRY_SOURCE_DIR) + "/tests/parity/golden/test_pca_model.json";
        // Using a dummy grpc target
        server = std::make_unique<ApiServer>("localhost:50051", mock_db);
    }
};

TEST_F(ApiScoringTest, ScoringJobFailsOnInsertError) {
    // 1. Setup mock behaviors
    mock_db->should_fail_insert = true;
    
    // We need to mock more methods for the scoring job to proceed
    // HandleScoreDatasetJob first calls CreateScoreJob
    // Then it starts a job which calls GetScoreJob, GetDatasetRecordCount, UpdateScoreJob, GetModelRun, FetchScoringRowsAfterRecord...
    
    // For now, let's just test that it reaches InsertDatasetScores and then Fails.
    
    httplib::Request req;
    req.body = R"({"dataset_id": "ds-1", "model_run_id": "model-1"})";
    httplib::Response res;
    
    ApiServerTestPeer::HandleScoreDatasetJob(*server, req, res);
    
    // Response should be 202 Accepted (async job started)
    EXPECT_EQ(res.status, 202);
    
    // Wait for the background job to run and fail
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify that UpdateScoreJob was called with FAILED
    EXPECT_EQ(mock_db->last_job_status, "FAILED");
    EXPECT_EQ(mock_db->last_job_error, "Simulated insert failure");
}

TEST_F(ApiScoringTest, ScoringJobFailsOnFetchError) {
    mock_db->should_fail_fetch = true;
    
    httplib::Request req;
    req.body = R"({"dataset_id": "ds-1", "model_run_id": "model-1"})";
    httplib::Response res;
    
    ApiServerTestPeer::HandleScoreDatasetJob(*server, req, res);
    
    EXPECT_EQ(res.status, 202);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(mock_db->last_job_status, "FAILED");
    EXPECT_EQ(mock_db->last_job_error, "Simulated fetch failure");
}

} // namespace telemetry::api
