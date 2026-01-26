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
    
    telemetry::RunStatus GetRunStatus(const std::string& run_id) override;

    std::string CreateModelRun(const std::string& dataset_id, const std::string& name) override;
    void UpdateModelRunStatus(const std::string& model_run_id, 
                                      const std::string& status, 
                                      const std::string& artifact_path = "", 
                                      const std::string& error = "") override;
    nlohmann::json GetModelRun(const std::string& model_run_id) override;

    std::string CreateInferenceRun(const std::string& model_run_id) override;
    void UpdateInferenceRunStatus(const std::string& inference_id, 
                                          const std::string& status, 
                                          int anomaly_count, 
                                          const nlohmann::json& details = {}) override;

    void InsertAlert(const Alert& alert);

private:
    std::string conn_str_;
};

