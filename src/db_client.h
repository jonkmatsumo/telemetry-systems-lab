#pragma once
#include "types.h"
#include <pqxx/pqxx>
#include <string>
#include <vector>
#include "telemetry.grpc.pb.h" // For RunStatus/Config types if needed

class DbClient {
public:
    DbClient(const std::string& connection_string);
    ~DbClient() = default;

    void CreateRun(const std::string& run_id, 
                   const telemetry::GenerateRequest& config, 
                   const std::string& status);
                   
    void UpdateRunStatus(const std::string& run_id, 
                         const std::string& status, 
                         long inserted_rows,
                         const std::string& error = "");

    void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records);

private:
    std::string conn_str_;
};
