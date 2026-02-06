#include <gtest/gtest.h>
#include "server.h"
#include "mocks/mock_db_client.h"
#include <thread>

namespace {

TEST(GeneratorLifecycleTest, EnforcesConcurrencyLimit) {
    auto factory = []() { return std::make_shared<MockDbClient>(); };
    TelemetryServiceImpl service(factory);
    service.SetMaxConcurrentJobs(1);
    
    telemetry::GenerateRequest req;
    req.set_start_time_iso("2026-01-01T00:00:00Z");
    req.set_end_time_iso("2026-01-01T01:00:00Z"); // 1 hour
    req.set_interval_seconds(60); // many rows
    req.set_host_count(10);
    
    telemetry::GenerateResponse resp1;
    auto status1 = service.GenerateTelemetry(nullptr, &req, &resp1);
    EXPECT_TRUE(status1.ok());
    
    telemetry::GenerateResponse resp2;
    auto status2 = service.GenerateTelemetry(nullptr, &req, &resp2);
    EXPECT_FALSE(status2.ok());
    EXPECT_EQ(status2.error_code(), grpc::StatusCode::RESOURCE_EXHAUSTED);
}

} // namespace