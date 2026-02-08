#include "db_client.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <string_view>
#include <vector>
#include "obs/metrics.h"
#include "pagination.h"
#include "obs/context.h"
#include <google/protobuf/util/json_util.h>
#include <fmt/chrono.h>
#include <algorithm>
#include <unordered_set>

// Compatibility macros for libpqxx 6.x vs 7.x
#if !defined(PQXX_VERSION_MAJOR) || (PQXX_VERSION_MAJOR < 7)
#define PQXX_EXEC_PREPPED(txn, stmt, ...) (txn).exec_prepared(stmt, __VA_ARGS__)
#define PQXX_EXEC_PARAMS(txn, query, ...) (txn).exec_params(query, __VA_ARGS__)
#else
#define PQXX_EXEC_PREPPED(txn, stmt, ...) (txn).exec(pqxx::prepped{stmt}, pqxx::params{__VA_ARGS__})
#define PQXX_EXEC_PARAMS(txn, query, ...) (txn).exec((query), pqxx::params{__VA_ARGS__})
#endif

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
DbClient::DbClient(const std::string& connection_string) 
    : manager_(std::make_shared<SimpleDbConnectionManager>(connection_string, [](pqxx::connection& C) {
        DbClient::PrepareStatements(C);
    })) {}

DbClient::DbClient(std::shared_ptr<DbConnectionManager> manager) 
    : manager_(std::move(manager)) {}

void DbClient::PrepareStatements(pqxx::connection& C) {
    C.prepare("insert_generation_run",
              "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config, request_id) "
              "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)");
    
    C.prepare("update_generation_run",
              "UPDATE generation_runs SET status = $1, inserted_rows = $2, updated_at = NOW() WHERE run_id = $3");
              
    C.prepare("update_generation_run_error",
              "UPDATE generation_runs SET status = $1, inserted_rows = $2, error = $3, updated_at = NOW() WHERE run_id = $4");
              
    C.prepare("get_run_status",
              "SELECT status, inserted_rows, error, request_id FROM generation_runs WHERE run_id = $1");

    C.prepare("heartbeat_generation", "UPDATE generation_runs SET updated_at = NOW() WHERE run_id = $1");
    C.prepare("heartbeat_model_run", "UPDATE model_runs SET updated_at = NOW() WHERE model_run_id = $1");
    C.prepare("heartbeat_score_job", "UPDATE dataset_score_jobs SET updated_at = NOW() WHERE job_id = $1");

    C.prepare("insert_alert",
              "INSERT INTO alerts (host_id, run_id, timestamp, severity, detector_source, score, details) "
              "VALUES ($1, $2, $3::timestamptz, $4, $5, $6, $7::jsonb)");

    C.prepare("insert_model_run",
              "INSERT INTO model_runs (dataset_id, name, status, request_id, training_config, hpo_config, candidate_fingerprint, generator_version, seed_used) "
              "VALUES ($1, $2, 'PENDING', $3, $4, $5, $6, $7, $8) RETURNING model_run_id");

    C.prepare("insert_hpo_trial",
              "INSERT INTO model_runs (dataset_id, name, status, request_id, training_config, parent_run_id, trial_index, trial_params) "
              "VALUES ($1, $2, 'PENDING', $3, $4, $5, $6, $7) RETURNING model_run_id");

    C.prepare("update_model_run_completed",
              "UPDATE model_runs SET status=$1, artifact_path=$2, completed_at=NOW(), updated_at=NOW() WHERE model_run_id=$3");

    C.prepare("update_model_run_failed",
              "UPDATE model_runs SET status=$1, error=$2, error_summary=$3, completed_at=NOW(), updated_at=NOW() WHERE model_run_id=$4");

    C.prepare("update_model_run_status",
              "UPDATE model_runs SET status=$1, updated_at=NOW() WHERE model_run_id=$2");

    C.prepare("get_model_run",
              "SELECT model_run_id, dataset_id, name, status, artifact_path, error, created_at, completed_at, request_id, training_config, "
              "hpo_config, parent_run_id, trial_index, trial_params, "
              "best_trial_run_id, best_metric_value, best_metric_name, "
              "selection_metric_direction, tie_break_basis, is_eligible, eligibility_reason, selection_metric_value, "
              "candidate_fingerprint, generator_version, seed_used, "
              "error_summary, error_aggregates, "
              "selection_metric_source, selection_metric_computed_at "
              "FROM model_runs WHERE model_run_id = $1");

    C.prepare("update_best_trial",
              "UPDATE model_runs SET best_trial_run_id=$1, best_metric_value=$2, best_metric_name=$3, "
              "selection_metric_direction=$4, tie_break_basis=$5 WHERE model_run_id=$6");

    C.prepare("update_trial_eligibility",
              "UPDATE model_runs SET is_eligible=$1, eligibility_reason=$2, selection_metric_value=$3, "
              "selection_metric_source=$4, selection_metric_computed_at=NOW() "
              "WHERE model_run_id=$5");

    C.prepare("update_error_aggregates",
              "UPDATE model_runs SET error_aggregates=$1 WHERE model_run_id=$2");

    C.prepare("get_hpo_trials_paginated",
              "SELECT model_run_id, status, trial_index, trial_params, created_at, completed_at, error, "
              "is_eligible, eligibility_reason, selection_metric_value, selection_metric_source, error_summary, dataset_id, name, training_config "
              "FROM model_runs WHERE parent_run_id = $1 ORDER BY trial_index ASC LIMIT $2 OFFSET $3");

    C.prepare("insert_inference_run",
              "INSERT INTO inference_runs (model_run_id, status) VALUES ($1, 'RUNNING') RETURNING inference_id");

    C.prepare("update_inference_run",
              "UPDATE inference_runs SET status=$1, anomaly_count=$2, details=$3::jsonb, latency_ms=$4 WHERE inference_id=$5");

    C.prepare("get_inference_run",
              "SELECT inference_id, model_run_id, status, anomaly_count, latency_ms, details, created_at "
              "FROM inference_runs WHERE inference_id = $1");

    C.prepare("get_models_for_dataset",
              "SELECT model_run_id, name, status, created_at FROM model_runs WHERE dataset_id = $1 ORDER BY created_at DESC");

    C.prepare("get_scored_datasets_for_model",
              "SELECT DISTINCT ds.dataset_id, gr.created_at, ds.scored_at "
              "FROM dataset_scores ds JOIN generation_runs gr ON ds.dataset_id = gr.run_id "
              "WHERE ds.model_run_id = $1 ORDER BY ds.scored_at DESC");

    C.prepare("get_dataset_detail",
              "SELECT run_id, status, inserted_rows, created_at, start_time, end_time, interval_seconds, host_count, tier, error, request_id "
              "FROM generation_runs WHERE run_id = $1");

    C.prepare("get_dataset_samples",
              "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate, metric_timestamp, host_id "
              "FROM host_telemetry_archival WHERE run_id = $1 ORDER BY metric_timestamp DESC LIMIT $2");

    C.prepare("get_dataset_record",
              "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate, metric_timestamp, host_id, labels "
              "FROM host_telemetry_archival WHERE run_id = $1 AND record_id = $2");

    C.prepare("check_score_job_exists",
              "SELECT job_id FROM dataset_score_jobs WHERE dataset_id = $1 AND model_run_id = $2 "
              "AND status IN ('PENDING', 'RUNNING')");

    C.prepare("insert_score_job",
              "INSERT INTO dataset_score_jobs (dataset_id, model_run_id, status, request_id) "
              "VALUES ($1, $2, 'PENDING', $3) RETURNING job_id");

    C.prepare("update_score_job_completed",
              "UPDATE dataset_score_jobs SET status=$1, total_rows=$2, processed_rows=$3, last_record_id=$4, updated_at=NOW(), completed_at=NOW() "
              "WHERE job_id=$5");

    C.prepare("update_score_job_error",
              "UPDATE dataset_score_jobs SET status=$1, total_rows=$2, processed_rows=$3, last_record_id=$4, error=$5, updated_at=NOW() "
              "WHERE job_id=$6");

    C.prepare("update_score_job_status",
              "UPDATE dataset_score_jobs SET status=$1, total_rows=$2, processed_rows=$3, last_record_id=$4, updated_at=NOW() "
              "WHERE job_id=$5");

    C.prepare("get_score_job",
              "SELECT job_id, dataset_id, model_run_id, status, total_rows, processed_rows, last_record_id, error, created_at, updated_at, completed_at, request_id "
              "FROM dataset_score_jobs WHERE job_id = $1");

    C.prepare("fetch_scoring_rows",
              "SELECT record_id, is_anomaly, cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
              "FROM host_telemetry_archival WHERE run_id = $1 AND record_id > $2 ORDER BY record_id ASC LIMIT $3");

    C.prepare("transition_model_run_status",
              "UPDATE model_runs SET status = $1 WHERE model_run_id = $2 AND status = $3");

    C.prepare("transition_score_job_status",
              "UPDATE dataset_score_jobs SET status = $1, updated_at = NOW() WHERE job_id = $2 AND status = $3");

    C.prepare("get_eval_metrics",
              "SELECT s.reconstruction_error, s.predicted_is_anomaly, h.is_anomaly "
              "FROM dataset_scores s JOIN host_telemetry_archival h ON s.record_id = h.record_id "
              "WHERE s.dataset_id = $1 AND s.model_run_id = $2");

    C.prepare("delete_dataset_scores", "DELETE FROM dataset_scores WHERE dataset_id = $1");
    C.prepare("delete_dataset_score_jobs", "DELETE FROM dataset_score_jobs WHERE dataset_id = $1");
    C.prepare("delete_host_telemetry", "DELETE FROM host_telemetry_archival WHERE run_id = $1");
    C.prepare("delete_alerts", "DELETE FROM alerts WHERE run_id = $1");
    C.prepare("delete_model_runs", "DELETE FROM model_runs WHERE dataset_id = $1");
    C.prepare("delete_generation_run", "DELETE FROM generation_runs WHERE run_id = $1");
    
    // Additional useful ones
    C.prepare("get_dataset_record_count", "SELECT COUNT(*) FROM host_telemetry_archival WHERE run_id = $1");

    C.prepare("get_non_anomaly_count", "SELECT COUNT(*) FROM host_telemetry_archival WHERE run_id = $1 AND is_anomaly = false");

    C.prepare("get_telemetry_batch",
              "SELECT record_id, cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
              "FROM host_telemetry_archival "
              "WHERE run_id = $1 AND record_id > $2 "
              "ORDER BY record_id "
              "LIMIT $3");

    C.prepare("call_cleanup", "CALL cleanup_old_telemetry($1::INT)");
    
    // Dynamic column selection requires quoting, but base WHERE clause is static for bounds/stats
    // For GetMetricStats/Histogram we need to inject the column name, so we can't fully prepare it 
    // unless we prepare one for each metric (too many) or interpolate the column name (which we do) 
    // and prepare the rest? No, prepared statement query string is fixed.
    // So if the query has dynamic column names "MIN(" + metric + ")", we can't use prepared statements 
    // unless we use the 'exec' approach with manual quoting. 
    // But RunRetentionCleanup is static.
}

// Static allowlist of valid metric column names from host_telemetry_archival schema.
// This prevents SQL injection via metric parameter in analytics queries.
bool DbClient::IsValidMetric(const std::string& metric) {
    static const std::unordered_set<std::string> kAllowedMetrics = {
        "cpu_usage",
        "memory_usage",
        "disk_utilization",
        "network_rx_rate",
        "network_tx_rate"
    };
    return kAllowedMetrics.count(metric) > 0;
}

bool DbClient::IsValidDimension(const std::string& dim) {
    static const std::unordered_set<std::string> kAllowedDimensions = {
        "region",
        "project_id",
        "host_id",
        "anomaly_type",
        "h.region",
        "h.project_id",
        "h.host_id",
        "h.anomaly_type"
    };
    return kAllowedDimensions.count(dim) > 0;
}

bool DbClient::IsValidAggregation(const std::string& agg) {
    static const std::unordered_set<std::string> kAllowedAggs = {
        "mean",
        "min",
        "max",
        "p50",
        "p95"
    };
    return kAllowedAggs.count(agg) > 0;
}

void DbClient::ReconcileStaleJobs(std::optional<std::chrono::seconds> stale_ttl) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        std::string condition = "status IN ('RUNNING', 'PENDING')";
        if (stale_ttl.has_value()) {
            condition += " AND updated_at < NOW() - INTERVAL '" + std::to_string(stale_ttl->count()) + " seconds'";
        }

        std::string error_msg = stale_ttl.has_value() ? "Stale job detected (heartbeat timeout)" : "System restart/recovery";

        PQXX_EXEC_PARAMS(W, "UPDATE dataset_score_jobs SET status='FAILED', error=" + W.quote(error_msg) + ", updated_at=NOW() WHERE " + condition);
        PQXX_EXEC_PARAMS(W, "UPDATE model_runs SET status='FAILED', error=" + W.quote(error_msg) + ", updated_at=NOW() WHERE " + condition);
        PQXX_EXEC_PARAMS(W, "UPDATE generation_runs SET status='FAILED', error=" + W.quote(error_msg) + ", updated_at=NOW() WHERE " + condition);
        
        W.commit();
        spdlog::info("Reconciled stale jobs (TTL={}).", stale_ttl.has_value() ? std::to_string(stale_ttl->count()) + "s" : "all");
    } catch (const std::exception& e) {
        spdlog::error("Failed to reconcile stale jobs: {}", e.what());
    }
}

void DbClient::RunRetentionCleanup(int retention_days) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        PQXX_EXEC_PREPPED(W, "call_cleanup", retention_days);
        W.commit();
        spdlog::info("Retention cleanup completed for data older than {} days.", retention_days);
    } catch (const std::exception& e) {
        spdlog::error("Failed to run retention cleanup: {}", e.what());
    }
}

void DbClient::EnsurePartition(std::chrono::system_clock::time_point tp) {
    try {
        auto t_time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&t_time);
        
        std::string part_name = fmt::format("host_telemetry_archival_{:04d}_{:02d}", tm.tm_year + 1900, tm.tm_mon + 1);
        std::string start_date = fmt::format("{:04d}-{:02d}-01", tm.tm_year + 1900, tm.tm_mon + 1);
        
        // Calculate end date (next month)
        int end_year = tm.tm_year + 1900;
        int end_mon = tm.tm_mon + 2;
        if (end_mon > 12) {
            end_mon = 1;
            end_year++;
        }
        std::string end_date = fmt::format("{:04d}-{:02d}-01", end_year, end_mon);

        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        std::string query = fmt::format(
            "CREATE TABLE IF NOT EXISTS {} PARTITION OF host_telemetry_archival "
            "FOR VALUES FROM ('{}') TO ('{}')",
            part_name, start_date, end_date);
            
        W.exec(query);
        W.commit();
        spdlog::info("Ensured partition {} exists for range [{}, {}).", part_name, start_date, end_date);
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure partition: {}", e.what());
    }
}

void DbClient::CreateRun(const std::string& run_id, 
                        const telemetry::GenerateRequest& config, 
                        const std::string& status,
                        const std::string& request_id) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        std::string config_json;
        (void)google::protobuf::util::MessageToJsonString(config, &config_json);

        PQXX_EXEC_PREPPED(W, "insert_generation_run",
                     run_id, config.tier(), config.host_count(), config.start_time_iso(), config.end_time_iso(), 
                     config.interval_seconds(), config.seed(), status, config_json, request_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create run: {}", e.what());
    }
}

void DbClient::UpdateRunStatus(const std::string& run_id, 
                              const std::string& status, 
                              long inserted_rows,
                              const std::string& error) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        if (!error.empty()) {
             PQXX_EXEC_PREPPED(W, "update_generation_run_error",
                          status, inserted_rows, error, run_id);
        } else {
             PQXX_EXEC_PREPPED(W, "update_generation_run",
                          status, inserted_rows, run_id);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update run status: {}", e.what());
    }
}

void DbClient::BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) {
    if (records.empty()) return;
    
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        const std::vector<std::string> columns = {
            "ingestion_time",
            "metric_timestamp",
            "host_id",
            "project_id",
            "region",
            "cpu_usage",
            "memory_usage",
            "disk_utilization",
            "network_rx_rate",
            "network_tx_rate",
            "labels",
            "run_id",
            "is_anomaly",
            "anomaly_type"};

#if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR >= 7)
        auto stream = pqxx::stream_to::table(W, pqxx::table_path{"host_telemetry_archival"}, 
            {std::string_view("ingestion_time"),
             std::string_view("metric_timestamp"),
             std::string_view("host_id"),
             std::string_view("project_id"),
             std::string_view("region"),
             std::string_view("cpu_usage"),
             std::string_view("memory_usage"),
             std::string_view("disk_utilization"),
             std::string_view("network_rx_rate"),
             std::string_view("network_tx_rate"),
             std::string_view("labels"),
             std::string_view("run_id"),
             std::string_view("is_anomaly"),
             std::string_view("anomaly_type")});
#else
        pqxx::stream_to stream(W, "host_telemetry_archival", columns);
#endif

        auto to_iso = [](std::chrono::system_clock::time_point tp) {
            return fmt::format("{:%Y-%m-%d %H:%M:%S%z}", tp);
        };

        for (const auto& r : records) {
             stream << std::make_tuple(
                    to_iso(r.ingestion_time),
                    to_iso(r.metric_timestamp),
                    r.host_id,
                    r.project_id,
                    r.region,
                    r.cpu_usage,
                    r.memory_usage,
                    r.disk_utilization,
                    r.network_rx_rate,
                    r.network_tx_rate,
                    r.labels_json,
                    r.run_id,
                    r.is_anomaly,
                    (r.anomaly_type.empty() ? nullptr : r.anomaly_type.c_str())
             );
        }
        stream.complete();

        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Batch insert failed: {}", e.what());
        throw;
    }
}

void DbClient::Heartbeat(JobType type, const std::string& job_id) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        std::string stmt;
        switch (type) {
            case JobType::Generation: stmt = "heartbeat_generation"; break;
            case JobType::ModelRun: stmt = "heartbeat_model_run"; break;
            case JobType::ScoreJob: stmt = "heartbeat_score_job"; break;
        }
        PQXX_EXEC_PREPPED(W, stmt, job_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to send heartbeat for job {}: {}", job_id, e.what());
    }
}

void DbClient::InsertAlert(const Alert& alert) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        auto to_iso = [](std::chrono::system_clock::time_point tp) {
            return fmt::format("{:%Y-%m-%d %H:%M:%S%z}", tp);
        };

        PQXX_EXEC_PREPPED(W, "insert_alert",
                      alert.host_id, alert.run_id, to_iso(alert.timestamp),
                      alert.severity, alert.source, alert.score, alert.details_json);
        W.commit();
        spdlog::info("Inserted alert for host {} severity {}", alert.host_id, alert.severity);
    } catch (const std::exception& e) {
        spdlog::error("Failed to insert alert: {}", e.what());
    }
}

telemetry::RunStatus DbClient::GetRunStatus(const std::string& run_id) {
    telemetry::RunStatus status;
    status.set_run_id(run_id);
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_run_status", run_id);
        if (!res.empty()) {
            status.set_status(res[0][0].as<std::string>());
            status.set_inserted_rows(res[0][1].as<long>());
            status.set_error(res[0][2].is_null() ? "" : res[0][2].as<std::string>());
            status.set_request_id(res[0][3].is_null() ? "" : res[0][3].as<std::string>());
        }
    } catch (const std::exception& e) {
        spdlog::error("DB Error in GetRunStatus for {}: {}", run_id, e.what());
        status.set_status("ERROR");
        status.set_error(e.what());
    }
    return status;
}

std::string DbClient::CreateModelRun(const std::string& dataset_id, 
                                     const std::string& name,
                                     const nlohmann::json& training_config,
                                     const std::string& request_id,
                                     const nlohmann::json& hpo_config,
                                     const std::string& candidate_fingerprint,
                                     const std::string& generator_version,
                                     std::optional<long long> seed_used) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        std::string hpo_val_str;
        const char* hpo_ptr = nullptr;
        if (!hpo_config.is_null() && !hpo_config.empty()) {
            hpo_val_str = hpo_config.dump();
            hpo_ptr = hpo_val_str.c_str();
        }

        const char* fp_ptr = candidate_fingerprint.empty() ? nullptr : candidate_fingerprint.c_str();
        const char* gv_ptr = generator_version.empty() ? nullptr : generator_version.c_str();

        auto res = PQXX_EXEC_PREPPED(W, "insert_model_run",
                                 dataset_id, name, request_id, training_config.dump(), hpo_ptr, fp_ptr, gv_ptr, seed_used);
        W.commit();
        if (!res.empty()) return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create model run: {}", e.what());
        throw;
    }
    return "";
}

std::string DbClient::CreateHpoTrialRun(const std::string& dataset_id,
                                        const std::string& name,
                                        const nlohmann::json& training_config,
                                        const std::string& request_id,
                                        const std::string& parent_run_id,
                                        int trial_index,
                                        const nlohmann::json& trial_params) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        auto res = PQXX_EXEC_PREPPED(W, "insert_hpo_trial",
                                 dataset_id, name, request_id, training_config.dump(), parent_run_id, trial_index, trial_params.dump());
        W.commit();
        if (!res.empty()) return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create HPO trial run: {}", e.what());
        throw;
    }
    return "";
}

void DbClient::UpdateModelRunStatus(const std::string& model_run_id, 
                                    const std::string& status, 
                                    const std::string& artifact_path, 
                                    const std::string& error,
                                    const nlohmann::json& error_summary) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        std::string summary_str;
        const char* summary_ptr = nullptr;
        if (!error_summary.is_null() && !error_summary.empty()) {
            summary_str = error_summary.dump();
            summary_ptr = summary_str.c_str();
        }

        if (status == "COMPLETED") {
             PQXX_EXEC_PREPPED(W, "update_model_run_completed",
                          status, artifact_path, model_run_id);
        } else if (status == "FAILED" || status == "CANCELLED" || status == "CANCELED") {
             PQXX_EXEC_PREPPED(W, "update_model_run_failed",
                          status, error, summary_ptr, model_run_id);
        } else {
             PQXX_EXEC_PREPPED(W, "update_model_run_status",
                          status, model_run_id);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update model run {}: {}", model_run_id, e.what());
    }
}

nlohmann::json DbClient::GetModelRun(const std::string& model_run_id) {

    nlohmann::json j;

    try {

        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;

        pqxx::nontransaction N(C);

        auto res = PQXX_EXEC_PREPPED(N, "get_model_run", model_run_id);

        if (!res.empty()) {
            // ...
            j["model_run_id"] = res[0][0].as<std::string>();
            // ... (keep middle assignments)
            j["dataset_id"] = res[0][1].as<std::string>();
            j["name"] = res[0][2].as<std::string>();
            j["status"] = res[0][3].as<std::string>();
            j["artifact_path"] = res[0][4].is_null() ? "" : res[0][4].as<std::string>();
            j["error"] = res[0][5].is_null() ? "" : res[0][5].as<std::string>();
            j["created_at"] = res[0][6].as<std::string>();
            j["completed_at"] = res[0][7].is_null() ? "" : res[0][7].as<std::string>();
            j["request_id"] = res[0][8].is_null() ? "" : res[0][8].as<std::string>();

            if (!res[0][9].is_null()) {
                try {
                    j["training_config"] = nlohmann::json::parse(res[0][9].as<std::string>());
                } catch (...) {
                    j["training_config"] = nlohmann::json::object();
                }
            } else {
                j["training_config"] = nlohmann::json::object();
            }

            if (!res[0][10].is_null()) {
                try {
                    j["hpo_config"] = nlohmann::json::parse(res[0][10].as<std::string>());
                } catch (...) {
                    j["hpo_config"] = nlohmann::json::object();
                }
            } else {
                j["hpo_config"] = nlohmann::json();
            }

            j["parent_run_id"] = res[0][11].is_null() ? nlohmann::json() : nlohmann::json(res[0][11].as<std::string>());
            j["trial_index"] = res[0][12].is_null() ? nlohmann::json() : nlohmann::json(res[0][12].as<int>());
            
            if (!res[0][13].is_null()) {
                try {
                    j["trial_params"] = nlohmann::json::parse(res[0][13].as<std::string>());
                } catch (...) {
                    j["trial_params"] = nlohmann::json::object();
                }
            } else {
                j["trial_params"] = nlohmann::json();
            }

            j["best_trial_run_id"] = res[0][14].is_null() ? nlohmann::json() : nlohmann::json(res[0][14].as<std::string>());
            j["best_metric_value"] = res[0][15].is_null() ? nlohmann::json() : nlohmann::json(res[0][15].as<double>());
            j["best_metric_name"] = res[0][16].is_null() ? nlohmann::json() : nlohmann::json(res[0][16].as<std::string>());

            j["selection_metric_direction"] = res[0][17].is_null() ? nlohmann::json() : nlohmann::json(res[0][17].as<std::string>());
            j["tie_break_basis"] = res[0][18].is_null() ? nlohmann::json() : nlohmann::json(res[0][18].as<std::string>());
            j["is_eligible"] = res[0][19].as<bool>();
            j["eligibility_reason"] = res[0][20].is_null() ? nlohmann::json() : nlohmann::json(res[0][20].as<std::string>());
            j["selection_metric_value"] = res[0][21].is_null() ? nlohmann::json() : nlohmann::json(res[0][21].as<double>());

            j["candidate_fingerprint"] = res[0][22].is_null() ? nlohmann::json() : nlohmann::json(res[0][22].as<std::string>());
            j["generator_version"] = res[0][23].is_null() ? nlohmann::json() : nlohmann::json(res[0][23].as<std::string>());
            j["seed_used"] = res[0][24].is_null() ? nlohmann::json() : nlohmann::json(res[0][24].as<long long>());

            if (!res[0][25].is_null()) {
                try {
                    j["error_summary"] = nlohmann::json::parse(res[0][25].as<std::string>());
                } catch (...) {
                    j["error_summary"] = nlohmann::json::object();
                }
            } else {
                j["error_summary"] = nlohmann::json();
            }

            if (!res[0][26].is_null()) {
                try {
                    j["error_aggregates"] = nlohmann::json::parse(res[0][26].as<std::string>());
                } catch (...) {
                    j["error_aggregates"] = nlohmann::json::object();
                }
            } else {
                j["error_aggregates"] = nlohmann::json();
            }

            j["selection_metric_source"] = res[0][27].is_null() ? nlohmann::json() : nlohmann::json(res[0][27].as<std::string>());
            j["selection_metric_computed_at"] = res[0][28].is_null() ? nlohmann::json() : nlohmann::json(res[0][28].as<std::string>());
        }

    } catch (const std::exception& e) {
        spdlog::error("DB Error in GetModelRun for {}: {}", model_run_id, e.what());
    }
    return j;
}

void DbClient::UpdateBestTrial(const std::string& parent_run_id,
                               const std::string& best_trial_run_id,
                               double best_metric_value,
                               const std::string& best_metric_name,
                               const std::string& best_metric_direction,
                               const std::string& tie_break_basis) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        PQXX_EXEC_PREPPED(W, "update_best_trial",
                     best_trial_run_id, best_metric_value, best_metric_name, 
                     best_metric_direction, tie_break_basis, parent_run_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update best trial for {}: {}", parent_run_id, e.what());
    }
}

void DbClient::UpdateTrialEligibility(const std::string& model_run_id,
                                     bool is_eligible,
                                     const std::string& reason,
                                     double metric_value,
                                     const std::string& source) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        const char* src_ptr = source.empty() ? nullptr : source.c_str();

        PQXX_EXEC_PREPPED(W, "update_trial_eligibility",
                      is_eligible, reason, metric_value, src_ptr, model_run_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update trial eligibility for {}: {}", model_run_id, e.what());
    }
}

void DbClient::UpdateParentErrorAggregates(const std::string& parent_run_id,
                                           const nlohmann::json& error_aggregates) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        PQXX_EXEC_PREPPED(W, "update_error_aggregates",
                      error_aggregates.dump(), parent_run_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update error aggregates for {}: {}", parent_run_id, e.what());
    }
}

nlohmann::json DbClient::GetHpoTrials(const std::string& parent_run_id) {
    return GetHpoTrialsPaginated(parent_run_id, 1000, 0);
}

std::map<std::string, nlohmann::json> DbClient::GetBulkHpoTrialSummaries(const std::vector<std::string>& parent_run_ids) {
    std::map<std::string, nlohmann::json> summaries;
    if (parent_run_ids.empty()) return summaries;

    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        
        std::string in_clause = "";
        for (const auto& id : parent_run_ids) {
            if (!in_clause.empty()) in_clause += ", ";
            in_clause += N.quote(id);
        }
        
        std::string query = "SELECT parent_run_id, status, COUNT(*) FROM model_runs WHERE parent_run_id IN (" + in_clause + ") GROUP BY parent_run_id, status";
        
        auto res = N.exec(query); 
        
        for (const auto& row : res) {
            std::string pid = row[0].as<std::string>();
            std::string status = row[1].as<std::string>();
            long count = row[2].as<long>();
            
            if (!summaries.count(pid)) {
                 summaries[pid] = {
                     {"trial_count", 0},
                     {"completed_count", 0},
                     {"status_counts", {{"PENDING", 0}, {"RUNNING", 0}, {"COMPLETED", 0}, {"FAILED", 0}, {"converted_FAIL", 0}}}
                 };
            }
            
            summaries[pid]["trial_count"] = summaries[pid]["trial_count"].get<long>() + count;
            summaries[pid]["status_counts"][status] = summaries[pid]["status_counts"][status].get<long>() + count;
            if (status == "COMPLETED") {
                summaries[pid]["completed_count"] = summaries[pid]["completed_count"].get<long>() + count;
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to bulk fetch hpo summaries: {}", e.what());
    }
    return summaries;
}

nlohmann::json DbClient::GetHpoTrialsPaginated(const std::string& parent_run_id, int limit, int offset) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_hpo_trials_paginated",
             parent_run_id, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["model_run_id"] = row[0].as<std::string>();
            j["status"] = row[1].as<std::string>();
            j["trial_index"] = row[2].as<int>();
            j["trial_params"] = nlohmann::json::parse(row[3].as<std::string>());
            j["created_at"] = row[4].as<std::string>();
            j["completed_at"] = row[5].is_null() ? "" : row[5].as<std::string>();
            j["error"] = row[6].is_null() ? "" : row[6].as<std::string>();
            j["is_eligible"] = row[7].as<bool>();
            j["eligibility_reason"] = row[8].is_null() ? nlohmann::json() : nlohmann::json(row[8].as<std::string>());
            j["selection_metric_value"] = row[9].is_null() ? nlohmann::json() : nlohmann::json(row[9].as<double>());
            j["selection_metric_source"] = row[10].is_null() ? nlohmann::json() : nlohmann::json(row[10].as<std::string>());
            if (!row[11].is_null()) {
                try {
                    j["error_summary"] = nlohmann::json::parse(row[11].as<std::string>());
                } catch (...) { j["error_summary"] = nlohmann::json::object(); }
            } else { j["error_summary"] = nlohmann::json(); }
            j["dataset_id"] = row[12].as<std::string>();
            j["name"] = row[13].as<std::string>();
            if (!row[14].is_null()) {
                try {
                    j["training_config"] = nlohmann::json::parse(row[14].as<std::string>());
                } catch (...) { j["training_config"] = nlohmann::json::object(); }
            } else { j["training_config"] = nlohmann::json::object(); }
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get paginated HPO trials for parent {}: {}", parent_run_id, e.what());
    }
    return out;
}

std::string DbClient::CreateInferenceRun(const std::string& model_run_id) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        auto res = PQXX_EXEC_PREPPED(W, "insert_inference_run",
                                  model_run_id);
        W.commit();
        if (!res.empty()) return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create inference run: {}", e.what());
        throw;
    }
    return "";
}

void DbClient::UpdateInferenceRunStatus(const std::string& inference_id, 
                                        const std::string& status, 
                                        int anomaly_count, 
                                        const nlohmann::json& details,
                                        double latency_ms) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        PQXX_EXEC_PREPPED(W, "update_inference_run",
                      status, anomaly_count, details.dump(), latency_ms, inference_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update inference run {}: {}", inference_id, e.what());
    }
}

nlohmann::json DbClient::ListGenerationRuns(int limit,
                                            int offset,
                                            const std::string& status,
                                            const std::string& created_from,
                                            const std::string& created_to) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        std::string query =
            "SELECT run_id, status, inserted_rows, created_at, start_time, end_time, interval_seconds, host_count, tier "
            "FROM generation_runs ";
        std::vector<std::string> where;
        if (!status.empty()) where.push_back("status = " + N.quote(status));
        if (!created_from.empty()) where.push_back("created_at >= " + N.quote(created_from));
        if (!created_to.empty()) where.push_back("created_at <= " + N.quote(created_to));
        if (!where.empty()) {
            query += "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                query += where[i];
                if (i + 1 < where.size()) query += " AND ";
            }
            query += " ";
        }
        query += "ORDER BY created_at DESC LIMIT $1 OFFSET $2";
        auto res = PQXX_EXEC_PARAMS(N, query, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["run_id"] = row[0].as<std::string>();
            j["status"] = row[1].as<std::string>();
            j["inserted_rows"] = row[2].as<long>();
            j["created_at"] = row[3].as<std::string>();
            j["start_time"] = row[4].as<std::string>();
            j["end_time"] = row[5].as<std::string>();
            j["interval_seconds"] = row[6].as<int>();
            j["host_count"] = row[7].as<int>();
            j["tier"] = row[8].as<std::string>();
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to list generation runs: {}", e.what());
        throw;
    }
    return out;
}

nlohmann::json DbClient::GetDatasetDetail(const std::string& run_id) {
    nlohmann::json j;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_dataset_detail",
             run_id);
        if (!res.empty()) {
            j["run_id"] = res[0][0].as<std::string>();
            j["status"] = res[0][1].as<std::string>();
            j["inserted_rows"] = res[0][2].as<long>();
            j["created_at"] = res[0][3].as<std::string>();
            j["start_time"] = res[0][4].as<std::string>();
            j["end_time"] = res[0][5].as<std::string>();
            j["interval_seconds"] = res[0][6].as<int>();
            j["host_count"] = res[0][7].as<int>();
            j["tier"] = res[0][8].as<std::string>();
            j["error"] = res[0][9].is_null() ? "" : res[0][9].as<std::string>();
            j["request_id"] = res[0][10].is_null() ? "" : res[0][10].as<std::string>();
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get dataset detail {}: {}", run_id, e.what());
        throw;
    }
    return j;
}

nlohmann::json DbClient::GetDatasetSamples(const std::string& run_id, int limit) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_dataset_samples",
             run_id, limit);
        for (const auto& row : res) {
            nlohmann::json j;
            j["cpu_usage"] = row[0].as<double>();
            j["memory_usage"] = row[1].as<double>();
            j["disk_utilization"] = row[2].as<double>();
            j["network_rx_rate"] = row[3].as<double>();
            j["network_tx_rate"] = row[4].as<double>();
            j["timestamp"] = row[5].as<std::string>();
            j["host_id"] = row[6].as<std::string>();
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get dataset samples {}: {}", run_id, e.what());
        throw;
    }
    return out;
}

nlohmann::json DbClient::GetDatasetRecord(const std::string& run_id, long record_id) {
    nlohmann::json j;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_dataset_record",
             run_id, record_id);
        if (!res.empty()) {
            j["cpu_usage"] = res[0][0].as<double>();
            j["memory_usage"] = res[0][1].as<double>();
            j["disk_utilization"] = res[0][2].as<double>();
            j["network_rx_rate"] = res[0][3].as<double>();
            j["network_tx_rate"] = res[0][4].as<double>();
            j["timestamp"] = res[0][5].as<std::string>();
            j["host_id"] = res[0][6].as<std::string>();
            j["labels"] = nlohmann::json::parse(res[0][7].c_str());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get dataset record run {} record {}: {}", run_id, record_id, e.what());
    }
    return j;
}

nlohmann::json DbClient::ListModelRuns(int limit,
                                       int offset,
                                       const std::string& status,
                                       const std::string& dataset_id,
                                       const std::string& created_from,
                                       const std::string& created_to) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        std::string query =
            "SELECT model_run_id, dataset_id, name, status, artifact_path, error, created_at, completed_at, training_config, "
            "parent_run_id, trial_index, best_trial_run_id, best_metric_value, best_metric_name, "
            "is_eligible, eligibility_reason, selection_metric_value "
            "FROM model_runs ";
        std::vector<std::string> where;
        if (!status.empty()) where.push_back("status = " + N.quote(status));
        if (!dataset_id.empty()) where.push_back("dataset_id = " + N.quote(dataset_id));
        if (!created_from.empty()) where.push_back("created_at >= " + N.quote(created_from));
        if (!created_to.empty()) where.push_back("created_at <= " + N.quote(created_to));
        if (!where.empty()) {
            query += "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                query += where[i];
                if (i + 1 < where.size()) query += " AND ";
            }
            query += " ";
        }
        query += "ORDER BY created_at DESC LIMIT $1 OFFSET $2";
        auto res = PQXX_EXEC_PARAMS(N, query, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["model_run_id"] = row[0].as<std::string>();
            j["dataset_id"] = row[1].as<std::string>();
            j["name"] = row[2].as<std::string>();
            j["status"] = row[3].as<std::string>();
            j["artifact_path"] = row[4].is_null() ? "" : row[4].as<std::string>();
            j["error"] = row[5].is_null() ? "" : row[5].as<std::string>();
            j["created_at"] = row[6].as<std::string>();
            j["completed_at"] = row[7].is_null() ? "" : row[7].as<std::string>();
            if (!row[8].is_null()) {
                try {
                    j["training_config"] = nlohmann::json::parse(row[8].as<std::string>());
                } catch (...) {
                    j["training_config"] = nlohmann::json::object();
                }
            } else {
                j["training_config"] = nlohmann::json::object();
            }
            j["parent_run_id"] = row[9].is_null() ? nlohmann::json() : nlohmann::json(row[9].as<std::string>());
            j["trial_index"] = row[10].is_null() ? nlohmann::json() : nlohmann::json(row[10].as<int>());
            j["best_trial_run_id"] = row[11].is_null() ? nlohmann::json() : nlohmann::json(row[11].as<std::string>());
            j["best_metric_value"] = row[12].is_null() ? nlohmann::json() : nlohmann::json(row[12].as<double>());
            j["best_metric_name"] = row[13].is_null() ? nlohmann::json() : nlohmann::json(row[13].as<std::string>());
            j["is_eligible"] = row[14].as<bool>();
            j["eligibility_reason"] = row[15].is_null() ? nlohmann::json() : nlohmann::json(row[15].as<std::string>());
            j["selection_metric_value"] = row[16].is_null() ? nlohmann::json() : nlohmann::json(row[16].as<double>());
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to list model runs: {}", e.what());
    }
    return out;
}

nlohmann::json DbClient::ListInferenceRuns(const std::string& dataset_id,
                                           const std::string& model_run_id,
                                           int limit,
                                           int offset,
                                           const std::string& status,
                                           const std::string& created_from,
                                           const std::string& created_to) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        std::string base =
            "SELECT i.inference_id, i.model_run_id, m.dataset_id, i.status, i.anomaly_count, i.latency_ms, i.created_at "
            "FROM inference_runs i JOIN model_runs m ON i.model_run_id = m.model_run_id ";
        std::vector<std::string> where;
        if (!dataset_id.empty()) where.push_back("m.dataset_id = " + N.quote(dataset_id));
        if (!model_run_id.empty()) where.push_back("i.model_run_id = " + N.quote(model_run_id));
        if (!status.empty()) where.push_back("i.status = " + N.quote(status));
        if (!created_from.empty()) where.push_back("i.created_at >= " + N.quote(created_from));
        if (!created_to.empty()) where.push_back("i.created_at <= " + N.quote(created_to));
        if (!where.empty()) {
            base += "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                base += where[i];
                if (i + 1 < where.size()) base += " AND ";
            }
            base += " ";
        }
        base += "ORDER BY i.created_at DESC LIMIT $1 OFFSET $2";
        auto res = PQXX_EXEC_PARAMS(N, base, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["inference_id"] = row[0].as<std::string>();
            j["model_run_id"] = row[1].as<std::string>();
            j["dataset_id"] = row[2].as<std::string>();
            j["status"] = row[3].as<std::string>();
            j["anomaly_count"] = row[4].as<int>();
            j["latency_ms"] = row[5].is_null() ? 0.0 : row[5].as<double>();
            j["created_at"] = row[6].as<std::string>();
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to list inference runs: {}", e.what());
    }
    return out;
}

nlohmann::json DbClient::GetInferenceRun(const std::string& inference_id) {
    nlohmann::json j;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_inference_run",
             inference_id);
        if (!res.empty()) {
            j["inference_id"] = res[0][0].as<std::string>();
            j["model_run_id"] = res[0][1].as<std::string>();
            j["status"] = res[0][2].as<std::string>();
            j["anomaly_count"] = res[0][3].as<int>();
            j["latency_ms"] = res[0][4].is_null() ? 0.0 : res[0][4].as<double>();
            j["details"] = res[0][5].is_null() ? nlohmann::json::array() : nlohmann::json::parse(res[0][5].c_str());
            j["created_at"] = res[0][6].as<std::string>();
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get inference run {}: {}", inference_id, e.what());
    }
    return j;
}

nlohmann::json DbClient::GetModelsForDataset(const std::string& dataset_id) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_models_for_dataset",
             dataset_id);
        for (const auto& row : res) {
            nlohmann::json j;
            j["model_run_id"] = row[0].as<std::string>();
            j["name"] = row[1].as<std::string>();
            j["status"] = row[2].as<std::string>();
            j["created_at"] = row[3].as<std::string>();
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get models for dataset {}: {}", dataset_id, e.what());
    }
    return out;
}

nlohmann::json DbClient::GetScoredDatasetsForModel(const std::string& model_run_id) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        // We find unique datasets from dataset_scores for this model
        auto res = PQXX_EXEC_PREPPED(N, "get_scored_datasets_for_model",
             model_run_id);
        for (const auto& row : res) {
            nlohmann::json j;
            j["dataset_id"] = row[0].as<std::string>();
            j["created_at"] = row[1].as<std::string>();
            j["scored_at"] = row[2].as<std::string>();
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get scored datasets for model {}: {}", model_run_id, e.what());
    }
    return out;
}

nlohmann::json DbClient::GetDatasetSummary(const std::string& run_id, int topk) {
    nlohmann::json j;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        auto res = W.exec(
            "SELECT COUNT(*), MIN(metric_timestamp), MAX(metric_timestamp), "
            "SUM(CASE WHEN is_anomaly THEN 1 ELSE 0 END) "
            "FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id));
        if (!res.empty()) {
            long count = res[0][0].as<long>();
            j["row_count"] = count;
            j["time_range"]["min_ts"] = res[0][1].is_null() ? "" : res[0][1].as<std::string>();
            j["time_range"]["max_ts"] = res[0][2].is_null() ? "" : res[0][2].as<std::string>();
            long anomalies = res[0][3].is_null() ? 0 : res[0][3].as<long>();
            j["anomaly_rate"] = count > 0 ? static_cast<double>(anomalies) / static_cast<double>(count) : 0.0;
        }

        auto res_types = W.exec(
            "SELECT anomaly_type, COUNT(*) FROM host_telemetry_archival "
            "WHERE run_id = " + W.quote(run_id) + " AND is_anomaly = true AND anomaly_type IS NOT NULL "
            "GROUP BY anomaly_type ORDER BY COUNT(*) DESC");
        nlohmann::json type_counts = nlohmann::json::array();
        long other = 0;
        int idx = 0;
        for (const auto& row : res_types) {
            std::string type = row[0].as<std::string>();
            long cnt = row[1].as<long>();
            if (idx < topk) {
                nlohmann::json entry;
                entry["label"] = type;
                entry["count"] = cnt;
                type_counts.push_back(entry);
            } else {
                other += cnt;
            }
            idx++;
        }
        if (other > 0) {
            nlohmann::json entry;
            entry["label"] = "other";
            entry["count"] = other;
            type_counts.push_back(entry);
        }
        j["anomaly_type_counts"] = type_counts;

        auto res_distinct = W.exec(
            "SELECT COUNT(DISTINCT host_id), COUNT(DISTINCT project_id), COUNT(DISTINCT region) "
            "FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id));
        if (!res_distinct.empty()) {
            j["distinct_counts"]["host_id"] = res_distinct[0][0].as<long>();
            j["distinct_counts"]["project_id"] = res_distinct[0][1].as<long>();
            j["distinct_counts"]["region"] = res_distinct[0][2].as<long>();
        }

        auto res_latency = W.exec(
            "SELECT "
            "PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY EXTRACT(EPOCH FROM (ingestion_time - metric_timestamp))), "
            "PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY EXTRACT(EPOCH FROM (ingestion_time - metric_timestamp))) "
            "FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id));
        if (!res_latency.empty()) {
            j["ingestion_latency_p50"] = res_latency[0][0].is_null() ? 0.0 : res_latency[0][0].as<double>();
            j["ingestion_latency_p95"] = res_latency[0][1].is_null() ? 0.0 : res_latency[0][1].as<double>();
        }

        auto res_trend = W.exec(
            "WITH max_ts AS (SELECT MAX(metric_timestamp) AS max_ts FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id) + ") "
            "SELECT date_trunc('hour', h.metric_timestamp) AS bucket, "
            "COUNT(*) AS total, "
            "SUM(CASE WHEN h.is_anomaly THEN 1 ELSE 0 END) AS anomalies "
            "FROM host_telemetry_archival h, max_ts "
            "WHERE h.run_id = " + W.quote(run_id) + " AND h.metric_timestamp >= max_ts.max_ts - INTERVAL '24 hours' "
            "GROUP BY bucket ORDER BY bucket ASC");
        nlohmann::json trend = nlohmann::json::array();
        for (const auto& row : res_trend) {
            long total = row[1].is_null() ? 0 : row[1].as<long>();
            long anomalies = row[2].is_null() ? 0 : row[2].as<long>();
            double rate = total > 0 ? static_cast<double>(anomalies) / static_cast<double>(total) : 0.0;
            nlohmann::json entry;
            entry["ts"] = row[0].is_null() ? "" : row[0].as<std::string>();
            entry["anomaly_rate"] = rate;
            entry["total"] = total;
            trend.push_back(entry);
        }
        j["anomaly_rate_trend"] = trend;

        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get dataset summary {}: {}", run_id, e.what());
        throw; // Propagate the exception
    }
    return j;
}

nlohmann::json DbClient::GetTopK(const std::string& run_id,
                                 const std::string& column,
                                 int k,
                                 const std::string& region,
                                 const std::string& is_anomaly,
                                 const std::string& anomaly_type,
                                 const std::string& start_time,
                                 const std::string& end_time,
                                 bool include_total_distinct) {
    if (!IsValidDimension(column)) {
        throw std::invalid_argument("Invalid column: " + column);
    }
    nlohmann::json out;
    out["items"] = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        std::string filter = "WHERE run_id = " + W.quote(run_id);
        if (!region.empty()) {
            filter += " AND region = " + W.quote(region);
        }
        if (!is_anomaly.empty()) {
            filter += " AND is_anomaly = " + W.quote(is_anomaly == "true");
        }
        if (!anomaly_type.empty()) {
            filter += " AND anomaly_type = " + W.quote(anomaly_type);
        }
        if (!start_time.empty()) {
            filter += " AND metric_timestamp >= " + W.quote(start_time);
        }
        if (!end_time.empty()) {
            filter += " AND metric_timestamp <= " + W.quote(end_time);
        }

        if (include_total_distinct) {
            auto res_count = W.exec("SELECT COUNT(DISTINCT " + column + ") FROM host_telemetry_archival " + filter);
            out["total_distinct"] = res_count.empty() ? 0 : res_count[0][0].as<long>();
        }

        std::string query = "SELECT " + column + ", COUNT(*) FROM host_telemetry_archival " + filter +
                            " GROUP BY " + column + " ORDER BY COUNT(*) DESC LIMIT " + std::to_string(k + 1);
        auto res = W.exec(query);
        
        bool truncated = false;
        size_t count = 0;
        for (const auto& row : res) {
            if (count >= static_cast<size_t>(k)) {
                truncated = true;
                break;
            }
            nlohmann::json j;
            j["label"] = row[0].is_null() ? "" : row[0].as<std::string>();
            j["count"] = row[1].as<long>();
            out["items"].push_back(j);
            count++;
        }
        out["truncated"] = truncated;
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get topk for {}: {}", run_id, e.what());
        throw;
    }
    return out;
}

nlohmann::json DbClient::GetTimeSeries(const std::string& run_id,
                                       const std::vector<std::string>& metrics,
                                       const std::vector<std::string>& aggs,
                                       int bucket_seconds,
                                       const std::string& region,
                                       const std::string& is_anomaly,
                                       const std::string& anomaly_type,
                                       const std::string& start_time,
                                       const std::string& end_time) {
    // Validate all metrics against allowlist to prevent SQL injection
    for (const auto& metric : metrics) {
        if (!IsValidMetric(metric)) {
            throw std::invalid_argument("Invalid metric: " + metric);
        }
    }
    // Validate all aggregations against allowlist
    for (const auto& agg : aggs) {
        if (!IsValidAggregation(agg)) {
            throw std::invalid_argument("Invalid aggregation: " + agg);
        }
    }
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        std::string bucket_expr = "to_timestamp(floor(extract(epoch from metric_timestamp) / " +
                                  std::to_string(bucket_seconds) + ") * " + std::to_string(bucket_seconds) + ")";

        std::string select = bucket_expr + " AS bucket_ts";
        for (const auto& metric : metrics) {
            for (const auto& agg : aggs) {
                std::string col = metric;
                std::string alias = metric + "_" + agg;
                if (agg == "mean") select += ", AVG(" + col + ") AS " + alias;
                else if (agg == "min") select += ", MIN(" + col + ") AS " + alias;
                else if (agg == "max") select += ", MAX(" + col + ") AS " + alias;
                else if (agg == "p50") select += ", PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY " + col + ") AS " + alias;
                else if (agg == "p95") select += ", PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY " + col + ") AS " + alias;
            }
        }
        select += ", COUNT(*) AS bucket_count";

        std::string query = "SELECT " + select + " FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id);
        if (!region.empty()) {
            query += " AND region = " + W.quote(region);
        }
        if (!is_anomaly.empty()) {
            query += " AND is_anomaly = " + W.quote(is_anomaly == "true");
        }
        if (!anomaly_type.empty()) {
            query += " AND anomaly_type = " + W.quote(anomaly_type);
        }
        if (!start_time.empty()) {
            query += " AND metric_timestamp >= " + W.quote(start_time);
        }
        if (!end_time.empty()) {
            query += " AND metric_timestamp <= " + W.quote(end_time);
        }
        query += " GROUP BY bucket_ts ORDER BY bucket_ts ASC";
        auto res = W.exec(query);
        for (const auto& row : res) {
            nlohmann::json j;
            j["ts"] = row[0].as<std::string>();
            int col_idx = 1;
            for (const auto& metric : metrics) {
                for (const auto& agg : aggs) {
                    std::string key = metric + "_" + agg;
                    j[key] = row[col_idx].is_null() ? 0.0 : row[col_idx].as<double>();
                    col_idx++;
                }
            }
            j["count"] = row[col_idx].as<long>();
            out.push_back(j);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get timeseries {}: {}", run_id, e.what());
        throw;
    }
    return out;
}

nlohmann::json DbClient::GetHistogram(const std::string& run_id,
                                      const std::string& metric,
                                      int bins,
                                      double min_val,
                                      double max_val,
                                      const std::string& region,
                                      const std::string& is_anomaly,
                                      const std::string& anomaly_type,
                                      const std::string& start_time,
                                      const std::string& end_time) {
    // Validate metric against allowlist to prevent SQL injection
    if (!IsValidMetric(metric)) {
        throw std::invalid_argument("Invalid metric: " + metric);
    }
    const int MAX_BINS = 500;
    int requested_bins = bins;
    if (bins > MAX_BINS) bins = MAX_BINS;

    nlohmann::json out;
    out["requested_bins"] = requested_bins;
    out["edges"] = nlohmann::json::array();
    out["counts"] = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        if (max_val <= min_val) {
            auto res = W.exec(
                "SELECT MIN(" + metric + "), MAX(" + metric + ") FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id));
            if (!res.empty() && !res[0][0].is_null() && !res[0][1].is_null()) {
                min_val = res[0][0].as<double>();
                max_val = res[0][1].as<double>();
            }
        }
        if (max_val <= min_val) {
            return out;
        }

        double step = (max_val - min_val) / static_cast<double>(bins);
        for (int i = 0; i <= bins; ++i) {
            out["edges"].push_back(min_val + step * i);
        }

        std::string query = "SELECT width_bucket(" + metric + ", " + std::to_string(min_val) + ", " +
                            std::to_string(max_val) + ", " + std::to_string(bins) + ") AS b, COUNT(*) "
                            "FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id);
        if (!region.empty()) {
            query += " AND region = " + W.quote(region);
        }
        if (!is_anomaly.empty()) {
            query += " AND is_anomaly = " + W.quote(is_anomaly == "true");
        }
        if (!anomaly_type.empty()) {
            query += " AND anomaly_type = " + W.quote(anomaly_type);
        }
        if (!start_time.empty()) {
            query += " AND metric_timestamp >= " + W.quote(start_time);
        }
        if (!end_time.empty()) {
            query += " AND metric_timestamp <= " + W.quote(end_time);
        }
        query += " GROUP BY b ORDER BY b ASC";
        auto res = W.exec(query);

        std::vector<long> counts(static_cast<size_t>(bins), 0);
        for (const auto& row : res) {
            int b = row[0].as<int>();
            long cnt = row[1].as<long>();
            if (b >= 1 && b <= bins) {
                counts[static_cast<size_t>(b - 1)] = cnt;
            }
        }
        for (int i = 0; i < bins; ++i) {
            out["counts"].push_back(counts[static_cast<size_t>(i)]);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get histogram {}: {}", run_id, e.what());
        throw;
    }
    return out;
}

nlohmann::json DbClient::GetMetricStats(const std::string& run_id, const std::string& metric) {
    if (!IsValidMetric(metric)) {
        throw std::invalid_argument("Invalid metric: " + metric);
    }
    nlohmann::json j;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        auto res = W.exec(
            "SELECT COUNT(*), MIN(" + metric + "), MAX(" + metric + "), AVG(" + metric + "), "
            "PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY " + metric + "), "
            "PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY " + metric + ") "
            "FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id));
        if (!res.empty()) {
            j["count"] = res[0][0].as<long>();
            j["min"] = res[0][1].is_null() ? 0.0 : res[0][1].as<double>();
            j["max"] = res[0][2].is_null() ? 0.0 : res[0][2].as<double>();
            j["mean"] = res[0][3].is_null() ? 0.0 : res[0][3].as<double>();
            j["p50"] = res[0][4].is_null() ? 0.0 : res[0][4].as<double>();
            j["p95"] = res[0][5].is_null() ? 0.0 : res[0][5].as<double>();
            j["missing_count"] = 0; // Schema is NOT NULL
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get metric stats for run {} metric {}: {}", run_id, metric, e.what());
        throw;
    }
    return j;
}

nlohmann::json DbClient::GetDatasetMetricsSummary(const std::string& run_id) {
    static const std::vector<std::string> kMetrics = {
        "cpu_usage", "memory_usage", "disk_utilization", "network_rx_rate", "network_tx_rate"
    };
    nlohmann::json out = nlohmann::json::object();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        std::string select;
        for (size_t i = 0; i < kMetrics.size(); ++i) {
            select += "STDDEV(" + kMetrics[i] + ") AS " + kMetrics[i] + "_stddev";
            if (i + 1 < kMetrics.size()) select += ", ";
        }
        auto res = W.exec("SELECT " + select + " FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id));
        if (!res.empty()) {
            std::vector<std::pair<std::string, double>> stddevs;
            for (const auto& m : kMetrics) {
                double val = res[0][m + "_stddev"].is_null() ? 0.0 : res[0][m + "_stddev"].as<double>();
                stddevs.push_back({m, val});
            }
            // Sort by stddev descending
            std::sort(stddevs.begin(), stddevs.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });
            nlohmann::json high_variance = nlohmann::json::array();
            for (const auto& p : stddevs) {
                high_variance.push_back({{"key", p.first}, {"stddev", p.second}});
            }
            out["high_variance"] = high_variance;
            out["high_missingness"] = nlohmann::json::array(); // Not applicable with NOT NULL schema
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get dataset metrics summary for {}: {}", run_id, e.what());
        throw;
    }
    return out;
}

std::string DbClient::CreateScoreJob(const std::string& dataset_id, 
                                    const std::string& model_run_id,
                                    const std::string& request_id) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        
        // Check if job already exists
        auto existing = PQXX_EXEC_PREPPED(W, "check_score_job_exists",
             dataset_id, model_run_id);
        if (!existing.empty()) return existing[0][0].as<std::string>();

        auto res = PQXX_EXEC_PREPPED(W, "insert_score_job",
             dataset_id, model_run_id, request_id);
        W.commit();
        if (!res.empty()) return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create score job: {}", e.what());
        throw;
    }
    return "";
}

void DbClient::UpdateScoreJob(const std::string& job_id,
                              const std::string& status,
                              long total_rows,
                              long processed_rows,
                              long last_record_id,
                              const std::string& error) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        if (status == "COMPLETED") {
            PQXX_EXEC_PREPPED(W, "update_score_job_completed",
                 status, total_rows, processed_rows, last_record_id, job_id);
        } else if (!error.empty()) {
            PQXX_EXEC_PREPPED(W, "update_score_job_error",
                 status, total_rows, processed_rows, last_record_id, error, job_id);
        } else {
            PQXX_EXEC_PREPPED(W, "update_score_job_status",
                 status, total_rows, processed_rows, last_record_id, job_id);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update score job {}: {}", job_id, e.what());
    }
}

nlohmann::json DbClient::GetScoreJob(const std::string& job_id) {
    nlohmann::json j;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_score_job",
             job_id);
        if (!res.empty()) {
            j["job_id"] = res[0][0].as<std::string>();
            j["dataset_id"] = res[0][1].as<std::string>();
            j["model_run_id"] = res[0][2].as<std::string>();
            j["status"] = res[0][3].as<std::string>();
            j["total_rows"] = res[0][4].as<long>();
            j["processed_rows"] = res[0][5].as<long>();
            j["last_record_id"] = res[0][6].as<long>();
            j["error"] = res[0][7].is_null() ? "" : res[0][7].as<std::string>();
            j["created_at"] = res[0][8].as<std::string>();
            j["updated_at"] = res[0][9].as<std::string>();
            j["completed_at"] = res[0][10].is_null() ? "" : res[0][10].as<std::string>();
            j["request_id"] = res[0][11].is_null() ? "" : res[0][11].as<std::string>();
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get score job {}: {}", job_id, e.what());
    }
    return j;
}

nlohmann::json DbClient::ListScoreJobs(int limit,
                                       int offset,
                                       const std::string& status,
                                       const std::string& dataset_id,
                                       const std::string& model_run_id,
                                       const std::string& created_from,
                                       const std::string& created_to) {
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        std::string query =
            "SELECT job_id, dataset_id, model_run_id, status, total_rows, processed_rows, last_record_id, error, created_at, updated_at, completed_at "
            "FROM dataset_score_jobs ";
        std::vector<std::string> where;
        if (!status.empty()) where.push_back("status = " + N.quote(status));
        if (!dataset_id.empty()) where.push_back("dataset_id = " + N.quote(dataset_id));
        if (!model_run_id.empty()) where.push_back("model_run_id = " + N.quote(model_run_id));
        if (!created_from.empty()) where.push_back("created_at >= " + N.quote(created_from));
        if (!created_to.empty()) where.push_back("created_at <= " + N.quote(created_to));
        if (!where.empty()) {
            query += "WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                query += where[i];
                if (i + 1 < where.size()) query += " AND ";
            }
            query += " ";
        }
        query += "ORDER BY created_at DESC LIMIT $1 OFFSET $2";
        auto res = PQXX_EXEC_PARAMS(N, query, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["job_id"] = row[0].as<std::string>();
            j["dataset_id"] = row[1].as<std::string>();
            j["model_run_id"] = row[2].as<std::string>();
            j["status"] = row[3].as<std::string>();
            j["total_rows"] = row[4].as<long>();
            j["processed_rows"] = row[5].as<long>();
            j["last_record_id"] = row[6].as<long>();
            j["error"] = row[7].is_null() ? "" : row[7].as<std::string>();
            j["created_at"] = row[8].as<std::string>();
            j["updated_at"] = row[9].as<std::string>();
            j["completed_at"] = row[10].is_null() ? "" : row[10].as<std::string>();
            out.push_back(j);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to list score jobs: {}", e.what());
    }
    return out;
}

std::vector<IDbClient::ScoringRow> DbClient::FetchScoringRowsAfterRecord(const std::string& dataset_id,
                                                                        long last_record_id,
                                                                        int limit) {
    std::vector<IDbClient::ScoringRow> rows;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "fetch_scoring_rows",
             dataset_id, last_record_id, limit);
        rows.reserve(static_cast<size_t>(res.size()));
        for (const auto& row : res) {
            IDbClient::ScoringRow r;
            r.record_id = row[0].as<long>();
            r.is_anomaly = row[1].as<bool>();
            r.cpu = row[2].as<double>();
            r.mem = row[3].as<double>();
            r.disk = row[4].as<double>();
            r.rx = row[5].as<double>();
            r.tx = row[6].as<double>();
            rows.push_back(r);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to fetch scoring rows: {}", e.what());
        throw;
    }
    return rows;
}

void DbClient::InsertDatasetScores(const std::string& dataset_id,
                                   const std::string& model_run_id,
                                   const std::vector<std::pair<long, std::pair<double, bool>>>& scores) {
    if (scores.empty()) return;
    auto start = std::chrono::steady_clock::now();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        const std::vector<std::string> columns = {
            "dataset_id",
            "model_run_id",
            "record_id",
            "reconstruction_error",
            "predicted_is_anomaly"};
#if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR >= 7)
        auto stream = pqxx::stream_to::table(W, pqxx::table_path{"dataset_scores"}, 
            {std::string_view("dataset_id"),
             std::string_view("model_run_id"),
             std::string_view("record_id"),
             std::string_view("reconstruction_error"),
             std::string_view("predicted_is_anomaly")});
#else
        pqxx::stream_to stream(W, "dataset_scores", columns);
#endif
        for (const auto& entry : scores) {
            stream << std::make_tuple(dataset_id, model_run_id, entry.first, entry.second.first, entry.second.second);
        }
        stream.complete();
        W.commit();
        auto end = std::chrono::steady_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        telemetry::obs::EmitCounter("scores_insert_rows", static_cast<long>(scores.size()), "rows", "db",
                                    {{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
        telemetry::obs::EmitHistogram("scores_insert_duration_ms", duration_ms, "ms", "db",
                                      {{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
        nlohmann::json fields = {
            {"dataset_id", dataset_id},
            {"model_run_id", model_run_id},
            {"rows", static_cast<long>(scores.size())},
            {"duration_ms", duration_ms}
        };
        if (telemetry::obs::HasContext()) {
            const auto& ctx = telemetry::obs::GetContext();
            if (!ctx.request_id.empty()) fields["request_id"] = ctx.request_id;
            if (!ctx.score_job_id.empty()) fields["score_job_id"] = ctx.score_job_id;
        }
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "db_insert", "db", fields);
    } catch (const std::exception& e) {
        spdlog::error("Failed to insert dataset scores (dataset_id={}, model_run_id={}): {}", 
                      dataset_id, model_run_id, e.what());
        throw;
    }
}

long DbClient::GetDatasetRecordCount(const std::string& dataset_id) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_dataset_record_count", dataset_id);
        return res.empty() ? 0 : res[0][0].as<long>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get dataset record count: {}", e.what());
        throw;
    }
}

nlohmann::json DbClient::GetScores(const std::string& dataset_id,
                                   const std::string& model_run_id,
                                   int limit,
                                   int offset,
                                   bool only_anomalies,
                                   double min_score,
                                   double max_score) {
    nlohmann::json out = nlohmann::json::object();
    out["items"] = nlohmann::json::array();
    auto start = std::chrono::steady_clock::now();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        std::string where = "WHERE s.dataset_id = " + N.quote(dataset_id) + " AND s.model_run_id = " + N.quote(model_run_id);
        if (only_anomalies) {
            where += " AND s.predicted_is_anomaly = true";
        }
        if (min_score > 0) {
            where += " AND s.reconstruction_error >= " + std::to_string(min_score);
        }
        if (max_score > 0) {
            where += " AND s.reconstruction_error <= " + std::to_string(max_score);
        }

        std::string query =
            "SELECT s.score_id, s.record_id, s.reconstruction_error, s.predicted_is_anomaly, s.scored_at, "
            "h.metric_timestamp, h.host_id, h.is_anomaly as label "
            "FROM dataset_scores s JOIN host_telemetry_archival h ON s.record_id = h.record_id "
            + where + " ORDER BY s.reconstruction_error DESC, s.score_id DESC LIMIT $1 OFFSET $2";
        
        auto res = PQXX_EXEC_PARAMS(N, query, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["score_id"] = row[0].as<long>();
            j["record_id"] = row[1].as<long>();
            j["score"] = row[2].as<double>();
            j["is_anomaly"] = row[3].as<bool>();
            j["scored_at"] = row[4].as<std::string>();
            j["timestamp"] = row[5].as<std::string>();
            j["host_id"] = row[6].as<std::string>();
            j["label"] = row[7].as<bool>();
            out["items"].push_back(j);
        }

        // Include metadata in results response
        auto model_run = GetModelRun(model_run_id);
        if (!model_run.empty()) {
            out["training_config"] = model_run.value("training_config", nlohmann::json::object());
            out["hpo_config"] = model_run.value("hpo_config", nlohmann::json());
            out["parent_run_id"] = model_run.value("parent_run_id", nlohmann::json());
            out["trial_index"] = model_run.value("trial_index", nlohmann::json());
            out["trial_params"] = model_run.value("trial_params", nlohmann::json());
        }

        auto count_res = N.exec("SELECT COUNT(*) FROM dataset_scores s " + where);
        out["total"] = count_res.empty() ? 0 : count_res[0][0].as<long>();

        // Orphan detection: scores where record_id is missing from host_telemetry_archival
        std::string orphan_query = 
            "SELECT COUNT(*) FROM dataset_scores s "
            "LEFT JOIN host_telemetry_archival h ON s.record_id = h.record_id "
            "WHERE s.dataset_id = " + N.quote(dataset_id) + " AND s.model_run_id = " + N.quote(model_run_id) +
            " AND h.record_id IS NULL";
        auto orphan_res = N.exec(orphan_query);
        long orphan_count = orphan_res.empty() ? 0 : orphan_res[0][0].as<long>();
        if (orphan_count > 0) {
            spdlog::warn("Detected {} orphaned scores for dataset {} and model {}", orphan_count, dataset_id, model_run_id);
            telemetry::obs::EmitCounter("scores_orphan_count", orphan_count, "count", "db_client", 
                                        {{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
        }
        
        // Fetch global min/max for the dataset+model (ignoring filters) to drive UI sliders
        std::string range_query = "SELECT MIN(reconstruction_error), MAX(reconstruction_error) FROM dataset_scores WHERE dataset_id = " + N.quote(dataset_id) + " AND model_run_id = " + N.quote(model_run_id);
        auto range_res = N.exec(range_query);
        if (!range_res.empty() && !range_res[0][0].is_null()) {
             out["min_score"] = range_res[0][0].as<double>();
             out["max_score"] = range_res[0][1].as<double>();
        } else {
             out["min_score"] = 0.0;
             out["max_score"] = 10.0; // Default fallback
        }

        out["limit"] = limit;
        out["offset"] = offset;
        long total = out["total"].get<long>();
        int returned = static_cast<int>(out["items"].size());
        out["returned"] = returned;
        out["has_more"] = telemetry::api::HasMore(limit, offset, returned, total);

    } catch (const std::exception& e) {
        spdlog::error("Failed to get scores: {}", e.what());
    }
    auto end = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
    telemetry::obs::EmitHistogram("scores_query_duration_ms", duration_ms, "ms", "db",
                                  {{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
    nlohmann::json fields = {
        {"dataset_id", dataset_id},
        {"model_run_id", model_run_id},
        {"duration_ms", duration_ms},
        {"rows", out["items"].size()}
    };
    if (telemetry::obs::HasContext()) {
        const auto& ctx = telemetry::obs::GetContext();
        if (!ctx.request_id.empty()) fields["request_id"] = ctx.request_id;
    }
    telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "db_query", "db", fields);
    return out;
}

nlohmann::json DbClient::GetEvalMetrics(const std::string& dataset_id,
                                        const std::string& model_run_id,
                                        int points,
                                        int max_samples) {
    nlohmann::json out;
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        auto res = PQXX_EXEC_PREPPED(N, "get_eval_metrics",
             dataset_id, model_run_id);
        struct EvalRow { double err; bool pred; bool label; };
        std::vector<EvalRow> samples;
        samples.reserve(static_cast<size_t>(res.size()));
        for (const auto& row : res) {
            samples.push_back({row[0].as<double>(), row[1].as<bool>(), row[2].as<bool>()});
            if (max_samples > 0 && static_cast<int>(samples.size()) >= max_samples) break;
        }

        long tp = 0, fp = 0, tn = 0, fn = 0;
        for (const auto& s : samples) {
            if (s.pred && s.label) tp++;
            else if (s.pred && !s.label) fp++;
            else if (!s.pred && !s.label) tn++;
            else fn++;
        }
        out["confusion"] = {{"tp", tp}, {"fp", fp}, {"tn", tn}, {"fn", fn}};

        std::sort(samples.begin(), samples.end(),
                  [](const auto& a, const auto& b) { return a.err > b.err; });
        int n_points = points > 0 ? points : 50;
        n_points = std::min(n_points, 200);
        n_points = std::max(n_points, 10);

        long positives = 0;
        long negatives = 0;
        for (const auto& s : samples) {
            if (s.label) positives++;
            else negatives++;
        }

        nlohmann::json roc = nlohmann::json::array();
        nlohmann::json pr = nlohmann::json::array();
        if (!samples.empty()) {
            for (int i = 0; i < n_points; ++i) {
                size_t idx = static_cast<size_t>((static_cast<double>(i) / (n_points - 1)) * static_cast<double>(samples.size() - 1));
                double threshold = samples[idx].err;
                long ttp = 0, tfp = 0;
                for (const auto& s : samples) {
                    bool pred = s.err >= threshold;
                    if (pred && s.label) ttp++;
                    else if (pred && !s.label) tfp++;
                }
                double tpr = positives > 0 ? static_cast<double>(ttp) / static_cast<double>(positives) : 0.0;
                double fpr = negatives > 0 ? static_cast<double>(tfp) / static_cast<double>(negatives) : 0.0;
                double precision = (ttp + tfp) > 0 ? static_cast<double>(ttp) / static_cast<double>(ttp + tfp) : 0.0;
                double recall = tpr;
                roc.push_back({{"fpr", fpr}, {"tpr", tpr}, {"threshold", threshold}});
                pr.push_back({{"precision", precision}, {"recall", recall}, {"threshold", threshold}});
            }
        }
        out["roc"] = roc;
        out["pr"] = pr;

        // Include metadata in results response
        auto model_run = GetModelRun(model_run_id);
        if (!model_run.empty()) {
            out["training_config"] = model_run.value("training_config", nlohmann::json::object());
            out["hpo_config"] = model_run.value("hpo_config", nlohmann::json());
            out["parent_run_id"] = model_run.value("parent_run_id", nlohmann::json());
            out["trial_index"] = model_run.value("trial_index", nlohmann::json());
            out["trial_params"] = model_run.value("trial_params", nlohmann::json());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get eval metrics: {}", e.what());
    }
    return out;
}

nlohmann::json DbClient::GetErrorDistribution(const std::string& dataset_id,
                                              const std::string& model_run_id,
                                              const std::string& group_by) {
    if (!IsValidDimension(group_by)) {
        throw std::invalid_argument("Invalid group_by: " + group_by);
    }
    nlohmann::json out = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        std::string col = group_by;
        std::string query =
            "SELECT " + col + ", "
            "COUNT(*), AVG(s.reconstruction_error), "
            "PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY s.reconstruction_error), "
            "PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY s.reconstruction_error) "
            "FROM dataset_scores s JOIN host_telemetry_archival h ON s.record_id = h.record_id "
            "WHERE s.dataset_id = " + W.quote(dataset_id) + " AND s.model_run_id = " + W.quote(model_run_id) +
            " GROUP BY " + col + " ORDER BY COUNT(*) DESC";
        auto res = W.exec(query);
        for (const auto& row : res) {
            nlohmann::json j;
            j["label"] = row[0].is_null() ? "" : row[0].as<std::string>();
            j["count"] = row[1].as<long>();
            j["mean"] = row[2].is_null() ? 0.0 : row[2].as<double>();
            j["p50"] = row[3].is_null() ? 0.0 : row[3].as<double>();
            j["p95"] = row[4].is_null() ? 0.0 : row[4].as<double>();
            out.push_back(j);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to get error distribution: {}", e.what());
        throw;
    }
    return out;
}

void DbClient::DeleteDatasetWithScores(const std::string& dataset_id) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);

        // 1. Delete scores
        PQXX_EXEC_PREPPED(W, "delete_dataset_scores", dataset_id);
        
        // 2. Delete score jobs
        PQXX_EXEC_PREPPED(W, "delete_dataset_score_jobs", dataset_id);
        
        // 3. Delete telemetry (archival)
        PQXX_EXEC_PREPPED(W, "delete_host_telemetry", dataset_id);
        
        // 4. Delete alerts
        PQXX_EXEC_PREPPED(W, "delete_alerts", dataset_id);

        // 5. Delete model runs
        PQXX_EXEC_PREPPED(W, "delete_model_runs", dataset_id);

        // 6. Delete the run itself
        PQXX_EXEC_PREPPED(W, "delete_generation_run", dataset_id);

        W.commit();
        spdlog::info("Successfully deleted dataset {} and all associated data.", dataset_id);
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete dataset {}: {}", dataset_id, e.what());
        throw;
    }
}

nlohmann::json DbClient::SearchDatasetRecords(const std::string& run_id,
                                              int limit,
                                              int offset,
                                              const std::string& start_time,
                                              const std::string& end_time,
                                              const std::string& is_anomaly,
                                              const std::string& anomaly_type,
                                              const std::string& host_id,
                                              const std::string& region,
                                              const std::string& sort_by,
                                              const std::string& sort_order,
                                              const std::string& anchor_time) {
    nlohmann::json out = nlohmann::json::object();
    out["items"] = nlohmann::json::array();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::nontransaction N(C);
        
        std::string sort_column = "metric_timestamp";
        if (!sort_by.empty() && sort_by != "metric_timestamp") {
            throw std::invalid_argument("Invalid sort_by: " + sort_by);
        }
        std::string sort_dir = "desc";
        if (!sort_order.empty()) {
            std::string lower = sort_order;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower != "asc" && lower != "desc") {
                throw std::invalid_argument("Invalid sort_order: " + sort_order);
            }
            sort_dir = lower;
        }

        std::string where = "WHERE run_id = " + N.quote(run_id);
        if (!start_time.empty()) where += " AND metric_timestamp >= " + N.quote(start_time);
        if (!end_time.empty()) where += " AND metric_timestamp <= " + N.quote(end_time);
        if (!anchor_time.empty()) {
            if (sort_dir == "asc") {
                where += " AND metric_timestamp >= " + N.quote(anchor_time);
            } else {
                where += " AND metric_timestamp <= " + N.quote(anchor_time);
            }
        }
        if (!is_anomaly.empty()) where += " AND is_anomaly = " + N.quote(is_anomaly == "true");
        if (!anomaly_type.empty()) where += " AND anomaly_type = " + N.quote(anomaly_type);
        if (!host_id.empty()) where += " AND host_id = " + N.quote(host_id);
        if (!region.empty()) where += " AND region = " + N.quote(region);

        std::string query =
            "SELECT record_id, host_id, metric_timestamp, cpu_usage, memory_usage, disk_utilization, "
            "network_rx_rate, network_tx_rate, is_anomaly, anomaly_type, region, project_id, labels "
            "FROM host_telemetry_archival " + where + " ORDER BY " + sort_column + " " + sort_dir + " LIMIT $1 OFFSET $2";
            
        auto res = PQXX_EXEC_PARAMS(N, query, limit, offset);
        for (const auto& row : res) {
            nlohmann::json j;
            j["record_id"] = row[0].as<long>();
            j["host_id"] = row[1].as<std::string>();
            j["timestamp"] = row[2].as<std::string>();
            j["cpu_usage"] = row[3].as<double>();
            j["memory_usage"] = row[4].as<double>();
            j["disk_utilization"] = row[5].as<double>();
            j["network_rx_rate"] = row[6].as<double>();
            j["network_tx_rate"] = row[7].as<double>();
            j["is_anomaly"] = row[8].as<bool>();
            j["anomaly_type"] = row[9].is_null() ? "" : row[9].as<std::string>();
            j["region"] = row[10].as<std::string>();
            j["project_id"] = row[11].as<std::string>();
            j["labels"] = row[12].is_null() ? nlohmann::json::object() : nlohmann::json::parse(row[12].c_str());
            out["items"].push_back(j);
        }
        
        auto count_res = N.exec("SELECT COUNT(*) FROM host_telemetry_archival " + where);
        long total = count_res.empty() ? 0 : count_res[0][0].as<long>();
        int returned = static_cast<int>(out["items"].size());
        out["total"] = total;
        out["limit"] = limit;
        out["offset"] = offset;
        out["returned"] = returned;
        out["has_more"] = telemetry::api::HasMore(limit, offset, returned, total);
        out["sort_by"] = sort_column;
        out["sort_order"] = sort_dir;
        if (!anchor_time.empty()) {
            out["anchor_time"] = anchor_time;
        }

    } catch (const std::exception& e) {
        spdlog::error("Failed to search dataset records: {}", e.what());
        throw;
    }
    return out;
}

bool DbClient::TryTransitionModelRunStatus(const std::string& model_run_id,
                                           const std::string& expected_current,
                                           const std::string& next_status) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        auto res = PQXX_EXEC_PREPPED(W, "transition_model_run_status",
                                  next_status, model_run_id, expected_current);
        W.commit();
        return res.affected_rows() > 0;
    } catch (const std::exception& e) {
        spdlog::error("Failed to transition model run status: {}", e.what());
        return false;
    }
}

bool DbClient::TryTransitionScoreJobStatus(const std::string& job_id,
                                           const std::string& expected_current,
                                           const std::string& next_status) {
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::work W(C);
        auto res = PQXX_EXEC_PREPPED(W, "transition_score_job_status",
                                  next_status, job_id, expected_current);
        W.commit();
        return res.affected_rows() > 0;
    } catch (const std::exception& e) {
        spdlog::error("Failed to transition score job status: {}", e.what());
        return false;
    }
}

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
