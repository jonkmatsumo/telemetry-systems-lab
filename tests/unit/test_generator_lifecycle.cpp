#include <gtest/gtest.h>
#include "server.h"
#include "mocks/mock_db_client.h"
#include <thread>

namespace {

TEST(GeneratorLifecycleTest, EnforcesConcurrencyLimit) {
    auto mock_db = std::make_shared<MockDbClient>();
    EXPECT_CALL(*mock_db, CreateRun(testing::_, testing::_, testing::_, testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*mock_db, Heartbeat(testing::_, testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*mock_db, UpdateRunStatus(testing::_, testing::_, testing::_, testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*mock_db, BatchInsertTelemetry(testing::_)).Times(testing::AnyNumber());

    auto factory = [mock_db]() { return mock_db; };
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