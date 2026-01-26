#include "server.h"
#include "generator.h"
#include "db_client.h"

#include <uuid/uuid.h>
#include <thread>
#include <chrono>

// Helper to generate UUID string
std::string GenerateUUID() {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char uuid[37];
    uuid_unparse_lower(binuuid, uuid);
    return std::string(uuid);
}

Status TelemetryServiceImpl::GenerateTelemetry(ServerContext* context, const GenerateRequest* request,
                                              GenerateResponse* response) {
    std::string run_id = GenerateUUID();
    spdlog::info("Received GenerateTelemetry request. Tier: {}, HostCount: {}, RunID: {}", 
                 request->tier(), request->host_count(), run_id);

    // Spawn background generation thread
    // Capture request by value to ensure valid lifetime
    GenerateRequest req_copy = *request;
    auto factory = db_factory_;
    
    std::thread([run_id, req_copy, factory]() {
        spdlog::info("Background generation for run {} started...", run_id);
        try {
            auto db = factory();
            Generator gen(req_copy, run_id, db);
            gen.Run();
        } catch (const std::exception& e) {
             spdlog::error("Thread for run {} failed: {}", run_id, e.what());
        }
        spdlog::info("Background generation for run {} finished.", run_id);
    }).detach();

    response->set_run_id(run_id);
    return Status::OK;
}


Status TelemetryServiceImpl::GetRun(ServerContext* context, const GetRunRequest* request,
                                   RunStatus* response) {
    spdlog::info("Received GetRun request for RunID: {}", request->run_id());
    
    auto db = db_factory_();
    *response = db->GetRunStatus(request->run_id());
    
    return Status::OK;
}
