#include <gtest/gtest.h>
#include "server.h"
#include "mocks/mock_db_client.h"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>

TEST(ServerTest, GenerateTelemetryReturnsUUID) {
    auto mock_db = std::make_shared<MockDbClient>();
    EXPECT_CALL(*mock_db, CreateRun(testing::_, testing::_, testing::_, testing::_)).Times(testing::AtLeast(0));
    EXPECT_CALL(*mock_db, UpdateRunStatus(testing::_, testing::_, testing::_, testing::_)).Times(testing::AtLeast(0));
    EXPECT_CALL(*mock_db, Heartbeat(testing::_, testing::_)).Times(testing::AtLeast(0));

    auto factory = [mock_db]() { return mock_db; };
    TelemetryServiceImpl service(factory);
    
    telemetry::GenerateRequest req;
    req.set_tier("TEST");
    req.set_host_count(5);
    
    telemetry::GenerateResponse resp;
    grpc::ServerContext context;
    
    grpc::Status status = service.GenerateTelemetry(&context, &req, &resp);
    
    EXPECT_TRUE(status.ok());
    EXPECT_FALSE(resp.run_id().empty());
}

TEST(ServerTest, GetRunReturnsStatus) {
    auto mock_db = std::make_shared<MockDbClient>();
    telemetry::RunStatus status_resp;
    status_resp.set_run_id("test-id");
    status_resp.set_status("RUNNING");
    
    EXPECT_CALL(*mock_db, GetRunStatus("test-id"))
        .WillOnce(testing::Return(status_resp));

    auto factory = [mock_db]() { return mock_db; };
    TelemetryServiceImpl service(factory);
    
    telemetry::GetRunRequest req;
    req.set_run_id("test-id");
    
    telemetry::RunStatus resp;
    grpc::ServerContext context;
    
    grpc::Status status = service.GetRun(&context, &req, &resp);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(resp.run_id(), "test-id");
    EXPECT_EQ(resp.status(), "RUNNING");
}
