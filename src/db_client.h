#pragma once
#include "idb_client.h"
#include <pqxx/pqxx>
#include <string>
#include <vector>

class DbClient : public IDbClient {
public:
    DbClient(const std::string& connection_string);
    ~DbClient() override = default;

    void CreateRun(const std::string& run_id, 
                   const telemetry::GenerateRequest& config, 
                   const std::string& status) override;
                   
    void UpdateRunStatus(const std::string& run_id, 
                         const std::string& status, 
                         long inserted_rows,
                         const std::string& error = "") override;

    void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) override;

private:
    std::string conn_str_;
};

