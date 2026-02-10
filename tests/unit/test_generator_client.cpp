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

// We can't easily mock the gRPC stub without more refactoring (e.g. template or interface for stub)
// But we can test the state machine logic if we make a few internal things accessible 
// or test via a real but failing endpoint.

TEST_F(GeneratorClientTest, BreakerOpensOnFailures) {
    // Point to a likely non-existent port to force failures
    GeneratorClient client("localhost:50099", GetTestConfig());
    
    GenerateRequest req;
    GenerateResponse res;
    
    // Attempt 1: Should fail and record failure
    auto status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(client.GetBreakerState(), GeneratorClient::BreakerState::Closed);
    
    // Attempt 2: Should fail and open breaker
    status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(client.GetBreakerState(), GeneratorClient::BreakerState::Open);
    
    // Attempt 3: Should be rejected immediately by breaker
    status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
    EXPECT_EQ(status.error_message(), "Circuit breaker is open");
}

TEST_F(GeneratorClientTest, BreakerHalfOpenAfterTimeout) {
    auto cfg = GetTestConfig();
    GeneratorClient client("localhost:50099", cfg);
    
    GenerateRequest req;
    GenerateResponse res;
    
    // Force open
    client.GenerateTelemetry(req, res);
    client.GenerateTelemetry(req, res);
    EXPECT_EQ(client.GetBreakerState(), GeneratorClient::BreakerState::Open);
    
    // Wait for timeout
    std::this_thread::sleep_for(cfg.breaker_timeout + std::chrono::milliseconds(50));
    
    // Next attempt should be allowed (HalfOpen)
    // It will still fail because endpoint is dead, but it shouldn't be "rejected" by breaker immediately
    auto status = client.GenerateTelemetry(req, res);
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.error_message(), "Circuit breaker is open");
}

} // namespace telemetry::api
