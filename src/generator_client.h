#pragma once

#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <chrono>
#include "telemetry.grpc.pb.h"

namespace telemetry::api {

struct GeneratorClientConfig {
    int max_retries = 3;
    std::chrono::milliseconds initial_backoff{100};
    std::chrono::milliseconds max_backoff{2000};
    int failure_threshold = 5;
    std::chrono::milliseconds breaker_timeout{30000};
    std::chrono::seconds call_timeout{10};
};

/**
 * @brief Resilient client for the Telemetry Generator service.
 * Implements retries with exponential backoff, circuit breaker, and keepalives.
 */
class GeneratorClient {
public:
    enum class BreakerState {
        Closed,
        Open,
        HalfOpen
    };

    GeneratorClient(std::string target, const GeneratorClientConfig& config = GeneratorClientConfig());
    virtual ~GeneratorClient() = default;

    auto GenerateTelemetry(const GenerateRequest& request, GenerateResponse& response) -> grpc::Status;
    auto GetRun(const GetRunRequest& request, RunStatus& response) -> grpc::Status;

    auto GetBreakerState() const -> BreakerState;

private:
    using GrpcCall = std::function<grpc::Status(grpc::ClientContext*)>;

    auto ExecuteWithResilience(const std::string& method_name, GrpcCall call) -> grpc::Status;
    auto ShouldRetry(const grpc::Status& status) -> bool;
    void RecordSuccess();
    void RecordFailure();
    auto CanAttempt() -> bool;

    std::string target_;
    GeneratorClientConfig config_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<telemetry::TelemetryService::Stub> stub_;

    mutable std::mutex mutex_;
    BreakerState state_ = BreakerState::Closed;
    int consecutive_failures_ = 0;
    std::chrono::steady_clock::time_point last_failure_time_;
};

} // namespace telemetry::api
