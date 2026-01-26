#include <gtest/gtest.h>
#include "server.h"
#include "mocks/mock_db_client.h"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>

TEST(ServerTest, GenerateTelemetryReturnsUUID) {
    auto factory = []() { return std::make_shared<MockDbClient>(); };
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
    auto factory = []() { return std::make_shared<MockDbClient>(); };
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
