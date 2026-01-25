#pragma once

#include "telemetry.grpc.pb.h"
#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using telemetry::TelemetryService;
using telemetry::GenerateRequest;
using telemetry::GenerateResponse;
using telemetry::GetRunRequest;
using telemetry::RunStatus;

class TelemetryServiceImpl final : public TelemetryService::Service {
public:
    Status GenerateTelemetry(ServerContext* context, const GenerateRequest* request,
                             GenerateResponse* response) override;

    Status GetRun(ServerContext* context, const GetRunRequest* request,
                  RunStatus* response) override;
};
