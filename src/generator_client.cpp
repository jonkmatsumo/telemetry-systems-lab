#include "generator_client.h"
#include <random>
#include <stdexcept>
#include <thread>
#include <spdlog/spdlog.h>
#include "obs/metrics.h"

namespace telemetry::api {

class GrpcGeneratorStubAdapter : public IGeneratorStub {
public:
    explicit GrpcGeneratorStubAdapter(std::unique_ptr<telemetry::TelemetryService::StubInterface> stub)
        : stub_(std::move(stub)) {}

    auto GenerateTelemetry(grpc::ClientContext& context, const telemetry::GenerateRequest& request, telemetry::GenerateResponse& response) -> grpc::Status override {
        return stub_->GenerateTelemetry(&context, request, &response);
    }

    auto GetRun(grpc::ClientContext& context, const telemetry::GetRunRequest& request, telemetry::RunStatus& response) -> grpc::Status override {
        return stub_->GetRun(&context, request, &response);
    }

private:
    std::unique_ptr<telemetry::TelemetryService::StubInterface> stub_;
};

GeneratorClient::GeneratorClient(std::string target, const GeneratorClientConfig& config)
    : target_(std::move(target)), config_(config) {
    
    grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    
    channel_ = grpc::CreateCustomChannel(target_, grpc::InsecureChannelCredentials(), args);
    stub_ = std::make_shared<GrpcGeneratorStubAdapter>(telemetry::TelemetryService::NewStub(channel_));
}

GeneratorClient::GeneratorClient(std::unique_ptr<IGeneratorStub> stub, const GeneratorClientConfig& config)
    : GeneratorClient(std::shared_ptr<IGeneratorStub>(std::move(stub)), config) {}

GeneratorClient::GeneratorClient(std::shared_ptr<IGeneratorStub> stub, const GeneratorClientConfig& config)
    : target_("in-memory"), config_(config), stub_(std::move(stub)) {
    if (!stub_) {
        throw std::invalid_argument("GeneratorClient stub must not be null");
    }
}

auto GeneratorClient::GenerateTelemetry(const GenerateRequest& request, GenerateResponse& response) -> grpc::Status {
    return ExecuteWithResilience("GenerateTelemetry", [this, &request, &response](grpc::ClientContext& context) {
        return stub_->GenerateTelemetry(context, request, response);
    });
}

auto GeneratorClient::GetRun(const GetRunRequest& request, RunStatus& response) -> grpc::Status {
    return ExecuteWithResilience("GetRun", [this, &request, &response](grpc::ClientContext& context) {
        return stub_->GetRun(context, request, response);
    });
}

auto GeneratorClient::GetBreakerState() const -> BreakerState {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

auto GeneratorClient::ExecuteWithResilience(const std::string& method_name, const GrpcCall& call) -> grpc::Status {
    if (!CanAttempt()) {
        telemetry::obs::EmitCounter("grpc_generator_rejected_breaker_total", 1, "count", "grpc", {{"method", method_name}});
        return {grpc::StatusCode::UNAVAILABLE, "Circuit breaker is open"};
    }

    grpc::Status status;
    std::chrono::milliseconds backoff = config_.initial_backoff;
    std::default_random_engine gen(std::random_device{}());

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + config_.call_timeout);

        // RPC is invoked synchronously while the per-attempt context is in scope.
        status = call(context);

        if (status.ok()) {
            RecordSuccess();
            if (attempt > 0) {
                telemetry::obs::EmitCounter("grpc_generator_retries_total", attempt, "count", "grpc", {{"method", method_name}, {"status", "OK"}});
            }
            return status;
        }

        if (!ShouldRetry(status)) {
            break;
        }

        if (attempt < config_.max_retries) {
            std::uniform_int_distribution<long> dist(0, backoff.count() / 4);
            auto jitter = std::chrono::milliseconds(dist(gen));
            std::this_thread::sleep_for(backoff + jitter);
            backoff = std::min(backoff * 2, config_.max_backoff);
            
            telemetry::obs::EmitCounter("grpc_generator_retries_total", 1, "count", "grpc", 
                                        {{"method", method_name}, {"status", std::to_string(static_cast<int>(status.error_code()))}});
        }
    }

    RecordFailure();
    telemetry::obs::EmitCounter("grpc_generator_errors_total", 1, "count", "grpc", 
                                {{"method", method_name}, {"status", std::to_string(static_cast<int>(status.error_code()))}});
    
    return status;
}

auto GeneratorClient::ShouldRetry(const grpc::Status& status) -> bool {
    auto code = status.error_code();
    return code == grpc::StatusCode::UNAVAILABLE || 
           code == grpc::StatusCode::DEADLINE_EXCEEDED ||
           code == grpc::StatusCode::INTERNAL;
}

void GeneratorClient::RecordSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == BreakerState::HalfOpen) {
        spdlog::info("GeneratorClient circuit breaker CLOSED for {}", target_);
    }
    state_ = BreakerState::Closed;
    consecutive_failures_ = 0;
    telemetry::obs::EmitGauge("grpc_generator_breaker_state", 0.0, "state", "grpc");
}

void GeneratorClient::RecordFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    consecutive_failures_++;
    last_failure_time_ = std::chrono::steady_clock::now();

    if (consecutive_failures_ >= config_.failure_threshold) {
        if (state_ != BreakerState::Open) {
            spdlog::warn("GeneratorClient circuit breaker OPEN for {}", target_);
        }
        state_ = BreakerState::Open;
        telemetry::obs::EmitGauge("grpc_generator_breaker_state", 1.0, "state", "grpc");
    }
}

auto GeneratorClient::CanAttempt() -> bool {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == BreakerState::Closed) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_failure_time_ > config_.breaker_timeout) {
        state_ = BreakerState::HalfOpen;
        telemetry::obs::EmitGauge("grpc_generator_breaker_state", 0.5, "state", "grpc");
        return true;
    }

    return false;
}

} // namespace telemetry::api
