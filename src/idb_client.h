#pragma once
#include "types.h"
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "telemetry.grpc.pb.h"

class IDbClient {
public:
    virtual ~IDbClient() = default;

    virtual void CreateRun(const std::string& run_id, 
                           const telemetry::GenerateRequest& config, 
                           const std::string& status,
                           const std::string& request_id = "") = 0;
                   
    virtual void UpdateRunStatus(const std::string& run_id, 
                                 const std::string& status, 
                                 long inserted_rows,
                                 const std::string& error = "") = 0;

    virtual void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) = 0;

    virtual telemetry::RunStatus GetRunStatus(const std::string& run_id) = 0;

    virtual std::string CreateModelRun(const std::string& dataset_id, 
                                       const std::string& name,
                                       const nlohmann::json& training_config = {},
                                       const std::string& request_id = "") = 0;
    virtual void UpdateModelRunStatus(const std::string& model_run_id, 
                                      const std::string& status, 
                                      const std::string& artifact_path = "", 
                                      const std::string& error = "") = 0;
    virtual nlohmann::json GetModelRun(const std::string& model_run_id) = 0;

    virtual std::string CreateInferenceRun(const std::string& model_run_id) = 0;
    virtual void UpdateInferenceRunStatus(const std::string& inference_id, 
                                          const std::string& status, 
                                          int anomaly_count, 
                                          const nlohmann::json& details = {},
                                          double latency_ms = 0.0) = 0;
};
