#pragma once

#include <grpcpp/grpcpp.h>
#include <functional>
#include <memory>
#include "telemetry.grpc.pb.h"
#include <spdlog/spdlog.h>
#include "db_client.h"
#include "idb_client.h"
#include "job_manager.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using telemetry::TelemetryService;
using telemetry::GenerateRequest;
using telemetry::GenerateResponse;
using telemetry::GetRunRequest;
using telemetry::RunStatus;

class TelemetryServiceImpl final : public ::telemetry::TelemetryService::Service {
public:
    using DbFactory = std::function<std::shared_ptr<IDbClient>()>;

    explicit TelemetryServiceImpl(std::string db_conn_str) 
        : db_conn_str_(std::move(db_conn_str)) 
    {
        db_factory_ = [this]() {
            return std::make_shared<DbClient>(db_conn_str_);
        };
        job_manager_ = std::make_unique<telemetry::JobManager>();
    }

    explicit TelemetryServiceImpl(DbFactory factory)
        : db_factory_(std::move(factory)) 
    {
        job_manager_ = std::make_unique<telemetry::JobManager>();
    }

    Status GenerateTelemetry(ServerContext* context, const GenerateRequest* request,
                             GenerateResponse* response) override;

    Status GetRun(ServerContext* context, const GetRunRequest* request,
                  RunStatus* response) override;

    void SetMaxConcurrentJobs(size_t n) { job_manager_->SetMaxConcurrentJobs(n); }

private:
    std::string db_conn_str_;
    DbFactory db_factory_;
    std::unique_ptr<telemetry::JobManager> job_manager_;
};

