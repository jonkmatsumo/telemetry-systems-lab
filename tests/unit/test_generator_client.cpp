#include <gtest/gtest.h>
#include "generator_client.h"
#include <thread>

namespace telemetry::api {

class GeneratorClientTest : public ::testing::Test {
protected:
    auto GetTestConfig() -> GeneratorClientConfig {
        GeneratorClientConfig cfg;
        cfg.max_retries = 1;
        cfg.initial_backoff = std::chrono::milliseconds(1);
        cfg.max_backoff = std::chrono::milliseconds(2);
        cfg.failure_threshold = 2;
        cfg.breaker_timeout = std::chrono::milliseconds(100);
        cfg.call_timeout = std::chrono::seconds(1);
        return cfg;
    }
};

class FakeGeneratorStub : public IGeneratorStub {
public:
    grpc::Status next_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "Unavailable");
    int call_count = 0;

    grpc::Status GenerateTelemetry(grpc::ClientContext*, const telemetry::GenerateRequest&, telemetry::GenerateResponse*) override {
        call_count++;
        return next_status;
    }
    grpc::Status GetRun(grpc::ClientContext*, const telemetry::GetRunRequest&, telemetry::RunStatus*) override {
        call_count++;
        return next_status;
    }
};

TEST_F(GeneratorClientTest, BreakerOpensOnFailures) {
    auto stub = std::make_shared<FakeGeneratorStub>();
    stub->next_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "Unavailable");
    
    GeneratorClient client(stub, GetTestConfig());
    
    GenerateRequest req;
    GenerateResponse res;
    
    // Attempt 1: Should fail and record failure
    auto status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(stub->call_count, 2);
    EXPECT_EQ(client.GetBreakerState(), GeneratorClient::BreakerState::Closed);
    
    // Attempt 2: Should fail and open breaker
    status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(stub->call_count, 4);
    EXPECT_EQ(client.GetBreakerState(), GeneratorClient::BreakerState::Open);
    
    // Attempt 3: Should be rejected immediately by breaker
    int count_before = stub->call_count;
    status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
    EXPECT_EQ(status.error_message(), "Circuit breaker is open");
    EXPECT_EQ(stub->call_count, count_before);
}

TEST_F(GeneratorClientTest, BreakerHalfOpenAfterTimeout) {
    auto cfg = GetTestConfig();
    auto stub = std::make_shared<FakeGeneratorStub>();
    stub->next_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "Unavailable");
    
    GeneratorClient client(stub, cfg);
    
    GenerateRequest req;
    GenerateResponse res;
    
    // Force open
    client.GenerateTelemetry(req, res);
    client.GenerateTelemetry(req, res);
    EXPECT_EQ(client.GetBreakerState(), GeneratorClient::BreakerState::Open);
    
    // Wait for timeout
    std::this_thread::sleep_for(cfg.breaker_timeout + std::chrono::milliseconds(50));
    
    // Next attempt should be allowed (HalfOpen)
    auto status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    // Should NOT be "Circuit breaker is open", but the actual error from stub
    EXPECT_NE(status.error_message(), "Circuit breaker is open");
    EXPECT_EQ(status.error_message(), "Unavailable");
}

} // namespace telemetry::api
