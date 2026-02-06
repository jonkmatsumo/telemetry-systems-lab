#pragma once
#include "idb_client.h"
#include <vector>
#include <string>

class MockDbClient : public IDbClient {
public:
    void CreateRun(const std::string& run_id, 
                   const telemetry::GenerateRequest& config, 
                   const std::string& status,
                   const std::string& request_id = "") override {
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

    telemetry::RunStatus GetRunStatus(const std::string& run_id) override {
        telemetry::RunStatus status;
        status.set_run_id(run_id);
        status.set_status("RUNNING"); // Match previous dummy behavior for tests
        status.set_inserted_rows(12345);
        return status;
    }

    std::string CreateModelRun(const std::string& dataset_id, 
                               const std::string& name,
                               const nlohmann::json& training_config = {},
                               const std::string& request_id = "",
                               const nlohmann::json& hpo_config = nlohmann::json::object(),
                               const std::string& candidate_fingerprint = "",
                               const std::string& generator_version = "",
                               std::optional<long long> seed_used = std::nullopt) override {
        return "mock-model-run-id";
    }

    std::string CreateHpoTrialRun(const std::string& dataset_id,
                                  const std::string& name,
                                  const nlohmann::json& training_config,
                                  const std::string& request_id,
                                  const std::string& parent_run_id,
                                  int trial_index,
                                  const nlohmann::json& trial_params) override {
        return "mock-trial-id";
    }

    void UpdateModelRunStatus(const std::string& model_run_id, 
                              const std::string& status, 
                              const std::string& artifact_path = "", 
                              const std::string& error = "",
                              const nlohmann::json& error_summary = nlohmann::json()) override {
        // No-op
    }

    nlohmann::json GetModelRun(const std::string& model_run_id) override {
        nlohmann::json j;
        j["model_run_id"] = model_run_id;
        j["status"] = "COMPLETED";
        j["artifact_path"] = "artifacts/pca/default/model.json";
        return j;
    }

    nlohmann::json GetHpoTrials(const std::string& parent_run_id) override {
        return nlohmann::json::array();
    }

    void UpdateBestTrial(const std::string& parent_run_id,
                         const std::string& best_trial_run_id,
                         double best_metric_value,
                         const std::string& best_metric_name,
                         const std::string& best_metric_direction,
                         const std::string& tie_break_basis) override {
        // No-op
    }

    std::string CreateInferenceRun(const std::string& model_run_id) override {
        return "mock-inference-id";
    }

    void UpdateInferenceRunStatus(const std::string& inference_id, 
                                  const std::string& status, 
                                  int anomaly_count, 
                                  const nlohmann::json& details = {},
                                  double latency_ms = 0.0) override {
        // No-op
    }

    void UpdateTrialEligibility(const std::string& model_run_id,
                                bool is_eligible,
                                const std::string& reason,
                                double metric_value) override {
        // No-op
    }

    void UpdateParentErrorAggregates(const std::string& parent_run_id,
                                     const nlohmann::json& error_aggregates) override {
        // No-op
    }

    // Inspection helpers
    size_t last_batch_size = 0;
    TelemetryRecord last_record;
};
