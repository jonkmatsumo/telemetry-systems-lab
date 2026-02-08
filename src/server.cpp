#include "server.h"
#include "generator.h"
#include "db_client.h"
#include "obs/context.h"

#include <uuid/uuid.h>
#include <thread>
#include <chrono>
#include <array>

// Helper to generate UUID string
auto GenerateUUID() -> std::string {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    std::array<char, 37> uuid{};
    uuid_unparse_lower(binuuid, uuid.data());
    return {uuid.data()};
}

auto TelemetryServiceImpl::GenerateTelemetry([[maybe_unused]] ServerContext* context, const GenerateRequest* request,
                                              GenerateResponse* response) -> Status {
    std::string run_id = GenerateUUID();
    spdlog::info("Received GenerateTelemetry request. Tier: {}, HostCount: {}, RunID: {}", 
                 request->tier(), request->host_count(), run_id);

    // Spawn background generation thread
    // Capture request by value to ensure valid lifetime
    GenerateRequest req_copy = *request;
    auto factory = db_factory_;
    
    try {
        job_manager_->StartJob("gen-" + run_id, req_copy.request_id(), [run_id, req_copy, factory](const std::atomic<bool>* stop_flag) {
            telemetry::obs::Context ctx;
            ctx.request_id = req_copy.request_id();
            ctx.dataset_id = run_id;
            telemetry::obs::ScopedContext scope(ctx);
            
            spdlog::info("Background generation for run {} started...", run_id);
            try {
                auto db = factory();
                Generator gen(req_copy, run_id, db);
                gen.SetStopFlag(stop_flag);
                gen.Run();
            } catch (const std::exception& e) {
                 spdlog::error("Thread for run {} failed: {}", run_id, e.what());
            }
            spdlog::info("Background generation for run {} finished.", run_id);
        });
    } catch (const std::exception& e) {
        spdlog::error("Failed to start generation job for run {}: {}", run_id, e.what());
        return {grpc::StatusCode::RESOURCE_EXHAUSTED, e.what()};
    }

    response->set_run_id(run_id);
    return Status::OK;
}


auto TelemetryServiceImpl::GetRun([[maybe_unused]] ServerContext* context, const GetRunRequest* request,
                                   RunStatus* response) -> Status {
    spdlog::info("Received GetRun request for RunID: {}", request->run_id());
    
    auto db = db_factory_();
    *response = db->GetRunStatus(request->run_id());
    
    return Status::OK;
}