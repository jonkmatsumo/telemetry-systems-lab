#pragma once
#include "types.h"
#include <vector>
#include <string>
#include "telemetry.grpc.pb.h"

class IDbClient {
public:
    virtual ~IDbClient() = default;

    virtual void CreateRun(const std::string& run_id, 
                           const telemetry::GenerateRequest& config, 
                           const std::string& status) = 0;
                   
    virtual void UpdateRunStatus(const std::string& run_id, 
                                 const std::string& status, 
                                 long inserted_rows,
                                 const std::string& error = "") = 0;

    virtual void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) = 0;
};
