#include <gtest/gtest.h>
#include "server.h"
#include <gtest/gtest.h>
#include "server.h"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>


// We can test the synchronous parts of TelemetryServiceImpl here.
// Since the generator runs in a detach thread and requires DB, 
// we mostly verify that the RPC returns expected status given inputs.
// In a fuller test we would mock the Generator spawning or DbClient injection.

TEST(ServerTest, GenerateTelemetryReturnsUUID) {
    // We pass a dummy connection string - if it actually tries to connect validation might fail
    // but in current impl connectivity is only checked inside the thread.
    TelemetryServiceImpl service("dummy_connection_string");
    
    telemetry::GenerateRequest req;
    req.set_tier("TEST");
    req.set_host_count(5);
    
    telemetry::GenerateResponse resp;
    grpc::ServerContext context;
    
    grpc::Status status = service.GenerateTelemetry(&context, &req, &resp);
    
    EXPECT_TRUE(status.ok());
    EXPECT_FALSE(resp.run_id().empty());
    EXPECT_EQ(resp.run_id().length(), 36); // UUID length
    
    // Give detached thread time to fail gracefully on dummy connection
    // before process teardown destroys logging/statics.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}


TEST(ServerTest, GetRunReturnsStatus) {
    TelemetryServiceImpl service("dummy");
    
    telemetry::GetRunRequest req;
    req.set_run_id("test-id");
    
    telemetry::RunStatus resp;
    grpc::ServerContext context;
    
    grpc::Status status = service.GetRun(&context, &req, &resp);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(resp.run_id(), "test-id");
    // Ensure skeleton behavior is present
    EXPECT_EQ(resp.status(), "RUNNING");
}
