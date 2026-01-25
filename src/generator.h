#pragma once
#include "types.h"
#include "db_client.h"
#include "telemetry.grpc.pb.h"
#include <atomic>

class Generator {
public:
    Generator(const telemetry::GenerateRequest& request, 
              std::string run_id, 
              std::shared_ptr<DbClient> db_client);

    void Run();

protected:
    telemetry::GenerateRequest config_;
    std::string run_id_;
    std::shared_ptr<DbClient> db_;
    
    std::vector<HostProfile> hosts_;
    
    void InitializeHosts();
    TelemetryRecord GenerateRecord(const HostProfile& host, 
                                   std::chrono::system_clock::time_point timestamp);
};

