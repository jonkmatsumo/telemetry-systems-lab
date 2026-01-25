#pragma once
#include "idb_client.h"
#include <vector>
#include <string>

class MockDbClient : public IDbClient {
public:
    void CreateRun(const std::string& run_id, 
                   const telemetry::GenerateRequest& config, 
                   const std::string& status) override {
        // No-op or record call
    }
                   
    void UpdateRunStatus(const std::string& run_id, 
                         const std::string& status, 
                         long inserted_rows,
                         const std::string& error = "") override {
        // No-op
    }

    void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) override {
        // No-op or capture records for verification
        last_batch_size = records.size();
        if (!records.empty()) {
            last_record = records.back();
        }
    }

    // Inspection helpers
    size_t last_batch_size = 0;
    TelemetryRecord last_record;
};
