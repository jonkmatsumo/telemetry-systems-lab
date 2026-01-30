#pragma once
#include "idb_client.h"
#include <pqxx/pqxx>
#include <string>
#include <vector>

class DbClient : public IDbClient {
public:
    DbClient(const std::string& connection_string);
    ~DbClient() override = default;

    // Validates that a metric name is a known telemetry column.
    // Returns true only for columns that exist in host_telemetry_archival schema.
    static bool IsValidMetric(const std::string& metric);

    // Validates that a dimension name is an allowed grouping/filtering column.
    static bool IsValidDimension(const std::string& dim);

    // Validates that an aggregation function is allowed.
    static bool IsValidAggregation(const std::string& agg);

    // Marks any 'RUNNING' jobs as 'FAILED' (called on startup).
    void ReconcileStaleJobs();

    // Runs the retention cleanup procedure.
    void RunRetentionCleanup(int retention_days);

    void CreateRun(const std::string& run_id, 
                   const telemetry::GenerateRequest& config, 
                   const std::string& status,
                   const std::string& request_id = "") override;
                   
    void UpdateRunStatus(const std::string& run_id, 
                         const std::string& status, 
                         long inserted_rows,
                         const std::string& error = "") override;

    void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) override;
    
    telemetry::RunStatus GetRunStatus(const std::string& run_id) override;

    std::string CreateModelRun(const std::string& dataset_id, 
                               const std::string& name,
                               const std::string& request_id = "") override;
    void UpdateModelRunStatus(const std::string& model_run_id, 
                                      const std::string& status, 
                                      const std::string& artifact_path = "", 
                                      const std::string& error = "") override;
    nlohmann::json GetModelRun(const std::string& model_run_id) override;

    std::string CreateInferenceRun(const std::string& model_run_id) override;
    void UpdateInferenceRunStatus(const std::string& inference_id, 
                                          const std::string& status, 
                                          int anomaly_count, 
                                          const nlohmann::json& details = {},
                                          double latency_ms = 0.0) override;

    void InsertAlert(const Alert& alert);

    nlohmann::json ListGenerationRuns(int limit,
                                      int offset,
                                      const std::string& status = "",
                                      const std::string& created_from = "",
                                      const std::string& created_to = "");
    nlohmann::json GetDatasetDetail(const std::string& run_id);
    nlohmann::json ListModelRuns(int limit,
                                 int offset,
                                 const std::string& status = "",
                                 const std::string& dataset_id = "",
                                 const std::string& created_from = "",
                                 const std::string& created_to = "");
    nlohmann::json ListInferenceRuns(const std::string& dataset_id,
                                     const std::string& model_run_id,
                                     int limit,
                                     int offset,
                                     const std::string& status = "",
                                     const std::string& created_from = "",
                                     const std::string& created_to = "");
    nlohmann::json GetInferenceRun(const std::string& inference_id);

    nlohmann::json GetDatasetSummary(const std::string& run_id, int topk);
    nlohmann::json GetTopK(const std::string& run_id,
                           const std::string& column,
                           int k,
                           const std::string& is_anomaly,
                           const std::string& anomaly_type,
                           const std::string& start_time,
                           const std::string& end_time);
    nlohmann::json GetTimeSeries(const std::string& run_id,
                                 const std::vector<std::string>& metrics,
                                 const std::vector<std::string>& aggs,
                                 int bucket_seconds,
                                 const std::string& is_anomaly,
                                 const std::string& anomaly_type,
                                 const std::string& start_time,
                                 const std::string& end_time);
    nlohmann::json GetHistogram(const std::string& run_id,
                                const std::string& metric,
                                int bins,
                                double min_val,
                                double max_val,
                                const std::string& is_anomaly,
                                const std::string& anomaly_type,
                                const std::string& start_time,
                                const std::string& end_time);

    std::string CreateScoreJob(const std::string& dataset_id, 
                               const std::string& model_run_id,
                               const std::string& request_id = "");
    void UpdateScoreJob(const std::string& job_id,
                        const std::string& status,
                        long total_rows,
                        long processed_rows,
                        long last_record_id = 0,
                        const std::string& error = "");
    nlohmann::json GetScoreJob(const std::string& job_id);
    nlohmann::json ListScoreJobs(int limit,
                                 int offset,
                                 const std::string& status = "",
                                 const std::string& dataset_id = "",
                                 const std::string& model_run_id = "",
                                 const std::string& created_from = "",
                                 const std::string& created_to = "");

    struct ScoringRow {
        long record_id = 0;
        bool is_anomaly = false;
        double cpu = 0.0;
        double mem = 0.0;
        double disk = 0.0;
        double rx = 0.0;
        double tx = 0.0;
    };
    std::vector<ScoringRow> FetchScoringRowsAfterRecord(const std::string& dataset_id,
                                                        long last_record_id,
                                                        int limit);
    void InsertDatasetScores(const std::string& dataset_id,
                             const std::string& model_run_id,
                             const std::vector<std::pair<long, std::pair<double, bool>>>& scores);

    nlohmann::json GetEvalMetrics(const std::string& dataset_id,
                                  const std::string& model_run_id,
                                  int points,
                                  int max_samples);
    nlohmann::json GetErrorDistribution(const std::string& dataset_id,
                                        const std::string& model_run_id,
                                        const std::string& group_by);

private:
    std::string conn_str_;
};
