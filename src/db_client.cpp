#include "db_client.h"
#include <spdlog/spdlog.h>
#include <google/protobuf/util/json_util.h>
#include <fmt/chrono.h>
#include <algorithm>
#include <unordered_set>


DbClient::DbClient(const std::string& connection_string) : conn_str_(connection_string) {}

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

void DbClient::ReconcileStaleJobs() {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        
        // Use exec() instead of exec0() to avoid potential issues if libpqxx version varies, though exec0 is standard.
        // Actually exec0 returns void, exec returns result.
        W.exec("UPDATE dataset_score_jobs SET status='FAILED', error='System restart/recovery' WHERE status='RUNNING'");
        W.exec("UPDATE model_runs SET status='FAILED', error='System restart/recovery' WHERE status='RUNNING'");
        W.exec("UPDATE generation_runs SET status='FAILED', error='System restart/recovery' WHERE status='RUNNING'");
        
        W.commit();
        spdlog::info("Reconciled stale RUNNING jobs to FAILED.");
    } catch (const std::exception& e) {
        spdlog::error("Failed to reconcile stale jobs: {}", e.what());
    }
}

void DbClient::RunRetentionCleanup(int retention_days) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        W.exec_params("CALL cleanup_old_telemetry($1)", retention_days);
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

        pqxx::connection C(conn_str_);
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        
        std::string config_json;
        google::protobuf::util::MessageToJsonString(config, &config_json);

        W.exec_params("INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config, request_id) "
                     "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        if (!error.empty()) {
             W.exec_params("UPDATE generation_runs SET status = $1, inserted_rows = $2, error = $3 WHERE run_id = $4",
                          status, inserted_rows, error, run_id);
        } else {
             W.exec_params("UPDATE generation_runs SET status = $1, inserted_rows = $2 WHERE run_id = $3",
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        
        auto stream = pqxx::stream_to::table(
            W,
            "host_telemetry_archival",
            {"ingestion_time",
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
             "anomaly_type"});

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

void DbClient::InsertAlert(const Alert& alert) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        
        auto to_iso = [](std::chrono::system_clock::time_point tp) {
            return fmt::format("{:%Y-%m-%d %H:%M:%S%z}", tp);
        };

        W.exec_params("INSERT INTO alerts (host_id, run_id, timestamp, severity, detector_source, score, details) "
                      "VALUES ($1, $2, $3::timestamptz, $4, $5, $6, $7::jsonb)",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params("SELECT status, inserted_rows, error, request_id FROM generation_runs WHERE run_id = $1", run_id);
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
                                     const std::string& request_id) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        auto res = W.exec_params("INSERT INTO model_runs (dataset_id, name, status, request_id) "
                                 "VALUES ($1, $2, 'PENDING', $3) RETURNING model_run_id",
                                 dataset_id, name, request_id);
        W.commit();
        if (!res.empty()) return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create model run: {}", e.what());
        throw;
    }
    return "";
}

void DbClient::UpdateModelRunStatus(const std::string& model_run_id, 
                                    const std::string& status, 
                                    const std::string& artifact_path, 
                                    const std::string& error) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        if (status == "COMPLETED") {
             W.exec_params("UPDATE model_runs SET status=$1, artifact_path=$2, completed_at=NOW() WHERE model_run_id=$3",
                          status, artifact_path, model_run_id);
        } else {
             W.exec_params("UPDATE model_runs SET status=$1, error=$2 WHERE model_run_id=$3",
                          status, error, model_run_id);
        }
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update model run {}: {}", model_run_id, e.what());
    }
}

nlohmann::json DbClient::GetModelRun(const std::string& model_run_id) {

    nlohmann::json j;

    try {

        pqxx::connection C(conn_str_);

        pqxx::nontransaction N(C);

        auto res = N.exec_params("SELECT model_run_id, dataset_id, name, status, artifact_path, error, created_at, completed_at, request_id "
                                 "FROM model_runs WHERE model_run_id = $1", model_run_id);

        if (!res.empty()) {

            j["model_run_id"] = res[0][0].as<std::string>();

            j["dataset_id"] = res[0][1].as<std::string>();

            j["name"] = res[0][2].as<std::string>();

            j["status"] = res[0][3].as<std::string>();

            j["artifact_path"] = res[0][4].is_null() ? "" : res[0][4].as<std::string>();

            j["error"] = res[0][5].is_null() ? "" : res[0][5].as<std::string>();

            j["created_at"] = res[0][6].as<std::string>();

            j["completed_at"] = res[0][7].is_null() ? "" : res[0][7].as<std::string>();

            j["request_id"] = res[0][8].is_null() ? "" : res[0][8].as<std::string>();

        }

    } catch (const std::exception& e) {
        spdlog::error("DB Error in GetModelRun for {}: {}", model_run_id, e.what());
    }
    return j;
}

std::string DbClient::CreateInferenceRun(const std::string& model_run_id) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        auto res = W.exec_params("INSERT INTO inference_runs (model_run_id, status) "
                                 "VALUES ($1, 'RUNNING') RETURNING inference_id",
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        W.exec_params("UPDATE inference_runs SET status=$1, anomaly_count=$2, details=$3::jsonb, latency_ms=$4 WHERE inference_id=$5",
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
        pqxx::connection C(conn_str_);
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
        auto res = N.exec_params(query, limit, offset);
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT run_id, status, inserted_rows, created_at, start_time, end_time, interval_seconds, host_count, tier, error, request_id "
            "FROM generation_runs WHERE run_id = $1",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate, metric_timestamp, host_id "
            "FROM host_telemetry_archival WHERE run_id = $1 ORDER BY metric_timestamp DESC LIMIT $2",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate, metric_timestamp, host_id, labels "
            "FROM host_telemetry_archival WHERE run_id = $1 AND record_id = $2",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        std::string query =
            "SELECT model_run_id, dataset_id, name, status, artifact_path, error, created_at, completed_at "
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
        auto res = N.exec_params(query, limit, offset);
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
        pqxx::connection C(conn_str_);
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
        auto res = N.exec_params(base, limit, offset);
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT inference_id, model_run_id, status, anomaly_count, latency_ms, details, created_at "
            "FROM inference_runs WHERE inference_id = $1",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT model_run_id, name, status, created_at FROM model_runs WHERE dataset_id = $1 ORDER BY created_at DESC",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        // We find unique datasets from dataset_scores for this model
        auto res = N.exec_params(
            "SELECT DISTINCT ds.dataset_id, gr.created_at, ds.scored_at "
            "FROM dataset_scores ds JOIN generation_runs gr ON ds.dataset_id = gr.run_id "
            "WHERE ds.model_run_id = $1 ORDER BY ds.scored_at DESC",
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        auto res = W.exec_params(
            "SELECT COUNT(*), MIN(metric_timestamp), MAX(metric_timestamp), "
            "SUM(CASE WHEN is_anomaly THEN 1 ELSE 0 END) "
            "FROM host_telemetry_archival WHERE run_id = $1",
            run_id);
        if (!res.empty()) {
            long count = res[0][0].as<long>();
            j["row_count"] = count;
            j["time_range"]["min_ts"] = res[0][1].is_null() ? "" : res[0][1].as<std::string>();
            j["time_range"]["max_ts"] = res[0][2].is_null() ? "" : res[0][2].as<std::string>();
            long anomalies = res[0][3].is_null() ? 0 : res[0][3].as<long>();
            j["anomaly_rate"] = count > 0 ? static_cast<double>(anomalies) / static_cast<double>(count) : 0.0;
        }

        auto res_types = W.exec_params(
            "SELECT anomaly_type, COUNT(*) FROM host_telemetry_archival "
            "WHERE run_id = $1 AND is_anomaly = true AND anomaly_type IS NOT NULL "
            "GROUP BY anomaly_type ORDER BY COUNT(*) DESC",
            run_id);
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

        auto res_distinct = W.exec_params(
            "SELECT COUNT(DISTINCT host_id), COUNT(DISTINCT project_id), COUNT(DISTINCT region) "
            "FROM host_telemetry_archival WHERE run_id = $1",
            run_id);
        if (!res_distinct.empty()) {
            j["distinct_counts"]["host_id"] = res_distinct[0][0].as<long>();
            j["distinct_counts"]["project_id"] = res_distinct[0][1].as<long>();
            j["distinct_counts"]["region"] = res_distinct[0][2].as<long>();
        }

        auto res_latency = W.exec_params(
            "SELECT "
            "PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY EXTRACT(EPOCH FROM (ingestion_time - metric_timestamp))), "
            "PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY EXTRACT(EPOCH FROM (ingestion_time - metric_timestamp))) "
            "FROM host_telemetry_archival WHERE run_id = $1",
            run_id);
        if (!res_latency.empty()) {
            j["ingestion_latency_p50"] = res_latency[0][0].is_null() ? 0.0 : res_latency[0][0].as<double>();
            j["ingestion_latency_p95"] = res_latency[0][1].is_null() ? 0.0 : res_latency[0][1].as<double>();
        }

        auto res_trend = W.exec_params(
            "WITH max_ts AS (SELECT MAX(metric_timestamp) AS max_ts FROM host_telemetry_archival WHERE run_id = $1) "
            "SELECT date_trunc('hour', h.metric_timestamp) AS bucket, "
            "COUNT(*) AS total, "
            "SUM(CASE WHEN h.is_anomaly THEN 1 ELSE 0 END) AS anomalies "
            "FROM host_telemetry_archival h, max_ts "
            "WHERE h.run_id = $1 AND h.metric_timestamp >= max_ts.max_ts - INTERVAL '24 hours' "
            "GROUP BY bucket ORDER BY bucket ASC",
            run_id);
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
                                 const std::string& is_anomaly,
                                 const std::string& anomaly_type,
                                 const std::string& start_time,
                                 const std::string& end_time) {
    if (!IsValidDimension(column)) {
        throw std::invalid_argument("Invalid column: " + column);
    }
    nlohmann::json out = nlohmann::json::array();
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        std::string query = "SELECT " + column + ", COUNT(*) FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id);
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
        query += " GROUP BY " + column + " ORDER BY COUNT(*) DESC LIMIT " + std::to_string(k);
        auto res = W.exec(query);
        for (const auto& row : res) {
            nlohmann::json j;
            j["label"] = row[0].is_null() ? "" : row[0].as<std::string>();
            j["count"] = row[1].as<long>();
            out.push_back(j);
        }
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
        pqxx::connection C(conn_str_);
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

        std::string query = "SELECT " + select + " FROM host_telemetry_archival WHERE run_id = " + W.quote(run_id);
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
                                      const std::string& is_anomaly,
                                      const std::string& anomaly_type,
                                      const std::string& start_time,
                                      const std::string& end_time) {
    // Validate metric against allowlist to prevent SQL injection
    if (!IsValidMetric(metric)) {
        throw std::invalid_argument("Invalid metric: " + metric);
    }
    nlohmann::json out;
    out["edges"] = nlohmann::json::array();
    out["counts"] = nlohmann::json::array();
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        if (max_val <= min_val) {
            auto res = W.exec_params(
                "SELECT MIN(" + metric + "), MAX(" + metric + ") FROM host_telemetry_archival WHERE run_id = $1",
                run_id);
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        auto res = W.exec_params(
            "SELECT COUNT(*), MIN(" + metric + "), MAX(" + metric + "), AVG(" + metric + "), "
            "PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY " + metric + "), "
            "PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY " + metric + ") "
            "FROM host_telemetry_archival WHERE run_id = $1",
            run_id);
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        std::string select;
        for (size_t i = 0; i < kMetrics.size(); ++i) {
            select += "STDDEV(" + kMetrics[i] + ") AS " + kMetrics[i] + "_stddev";
            if (i + 1 < kMetrics.size()) select += ", ";
        }
        auto res = W.exec_params("SELECT " + select + " FROM host_telemetry_archival WHERE run_id = $1", run_id);
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        
        // Check if job already exists
        auto existing = W.exec_params(
            "SELECT job_id FROM dataset_score_jobs WHERE dataset_id = $1 AND model_run_id = $2 "
            "AND status IN ('PENDING', 'RUNNING')",
            dataset_id, model_run_id);
        if (!existing.empty()) return existing[0][0].as<std::string>();

        auto res = W.exec_params(
            "INSERT INTO dataset_score_jobs (dataset_id, model_run_id, status, request_id) "
            "VALUES ($1, $2, 'PENDING', $3) RETURNING job_id",
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
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        if (status == "COMPLETED") {
            W.exec_params(
                "UPDATE dataset_score_jobs SET status=$1, total_rows=$2, processed_rows=$3, last_record_id=$4, updated_at=NOW(), completed_at=NOW() "
                "WHERE job_id=$5",
                status, total_rows, processed_rows, last_record_id, job_id);
        } else if (!error.empty()) {
            W.exec_params(
                "UPDATE dataset_score_jobs SET status=$1, total_rows=$2, processed_rows=$3, last_record_id=$4, error=$5, updated_at=NOW() "
                "WHERE job_id=$6",
                status, total_rows, processed_rows, last_record_id, error, job_id);
        } else {
            W.exec_params(
                "UPDATE dataset_score_jobs SET status=$1, total_rows=$2, processed_rows=$3, last_record_id=$4, updated_at=NOW() "
                "WHERE job_id=$5",
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
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT job_id, dataset_id, model_run_id, status, total_rows, processed_rows, last_record_id, error, created_at, updated_at, completed_at, request_id "
            "FROM dataset_score_jobs WHERE job_id = $1",
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
        pqxx::connection C(conn_str_);
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
        auto res = N.exec_params(query, limit, offset);
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

std::vector<DbClient::ScoringRow> DbClient::FetchScoringRowsAfterRecord(const std::string& dataset_id,
                                                                        long last_record_id,
                                                                        int limit) {
    std::vector<ScoringRow> rows;
    try {
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT record_id, is_anomaly, cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
            "FROM host_telemetry_archival WHERE run_id = $1 AND record_id > $2 ORDER BY record_id ASC LIMIT $3",
            dataset_id, last_record_id, limit);
        rows.reserve(res.size());
        for (const auto& row : res) {
            ScoringRow r;
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
    }
    return rows;
}

void DbClient::InsertDatasetScores(const std::string& dataset_id,
                                   const std::string& model_run_id,
                                   const std::vector<std::pair<long, std::pair<double, bool>>>& scores) {
    if (scores.empty()) return;
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        auto stream = pqxx::stream_to::table(
            W,
            "dataset_scores",
            {"dataset_id",
             "model_run_id",
             "record_id",
             "reconstruction_error",
             "predicted_is_anomaly"});
        for (const auto& entry : scores) {
            stream << std::make_tuple(dataset_id, model_run_id, entry.first, entry.second.first, entry.second.second);
        }
        stream.complete();
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to insert dataset scores: {}", e.what());
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
    try {
        pqxx::connection C(conn_str_);
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
        
        auto res = N.exec_params(query, limit, offset);
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

        auto count_res = N.exec("SELECT COUNT(*) FROM dataset_scores s " + where);
        out["total"] = count_res.empty() ? 0 : count_res[0][0].as<long>();
        
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

    } catch (const std::exception& e) {
        spdlog::error("Failed to get scores: {}", e.what());
    }
    return out;
}

nlohmann::json DbClient::GetEvalMetrics(const std::string& dataset_id,
                                        const std::string& model_run_id,
                                        int points,
                                        int max_samples) {
    nlohmann::json out;
    try {
        pqxx::connection C(conn_str_);
        pqxx::nontransaction N(C);
        auto res = N.exec_params(
            "SELECT s.reconstruction_error, s.predicted_is_anomaly, h.is_anomaly "
            "FROM dataset_scores s JOIN host_telemetry_archival h ON s.record_id = h.record_id "
            "WHERE s.dataset_id = $1 AND s.model_run_id = $2",
            dataset_id, model_run_id);
        struct EvalRow { double err; bool pred; bool label; };
        std::vector<EvalRow> samples;
        samples.reserve(res.size());
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
                size_t idx = static_cast<size_t>((static_cast<double>(i) / (n_points - 1)) * (samples.size() - 1));
                double threshold = samples[idx].err;
                long ttp = 0, tfp = 0, tfn = 0;
                for (const auto& s : samples) {
                    bool pred = s.err >= threshold;
                    if (pred && s.label) ttp++;
                    else if (pred && !s.label) tfp++;
                    else if (!pred && s.label) tfn++;
                }
                double tpr = positives > 0 ? static_cast<double>(ttp) / static_cast<double>(positives) : 0.0;
                double fpr = negatives > 0 ? static_cast<double>(tfp) / static_cast<double>(negatives) : 0.0;
                double precision = (ttp + tfp) > 0 ? static_cast<double>(ttp) / static_cast<double>(ttp + tfp) : 0.0;
                double recall = tpr;
                roc.push_back({{"fpr", fpr}, {"tpr", tpr}});
                pr.push_back({{"precision", precision}, {"recall", recall}});
            }
        }
        out["roc"] = roc;
        out["pr"] = pr;
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
        pqxx::connection C(conn_str_);
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
