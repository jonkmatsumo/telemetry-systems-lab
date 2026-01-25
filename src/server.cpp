#include "server.h"
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

    // TODO: Persist run status to DB (PENDING)
    
    // TODO: Spawn background generation thread
    // For skeleton verification, just log
    std::thread([run_id]() {
        spdlog::info("Background generation for run {} started...", run_id);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        spdlog::info("Background generation for run {} finished.", run_id);
    }).detach();

    response->set_run_id(run_id);
    return Status::OK;
}

Status TelemetryServiceImpl::GetRun(ServerContext* context, const GetRunRequest* request,
                                   RunStatus* response) {
    spdlog::info("Received GetRun request for RunID: {}", request->run_id());
    
    response->set_run_id(request->run_id());
    response->set_status("RUNNING"); // Dummy status
    response->set_inserted_rows(12345);
    
    return Status::OK;
}
