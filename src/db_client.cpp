#include "db_client.h"
#include <spdlog/spdlog.h>
#include <google/protobuf/util/json_util.h>
#include <fmt/chrono.h>


DbClient::DbClient(const std::string& connection_string) : conn_str_(connection_string) {}

void DbClient::CreateRun(const std::string& run_id, 
                        const telemetry::GenerateRequest& config, 
                        const std::string& status) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        
        std::string config_json;
        google::protobuf::util::MessageToJsonString(config, &config_json);

        W.exec_params("INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config) "
                      "VALUES ($1, $2, $3, $4::timestamptz, $5::timestamptz, $6, $7, $8, $9::jsonb)",
                      run_id, config.tier(), config.host_count(), 
                      config.start_time_iso(), config.end_time_iso(),
                      config.interval_seconds(), config.seed(),
                      status, config_json);
        W.commit();
        spdlog::info("Created run {} in DB", run_id);
    } catch (const std::exception& e) {
        spdlog::error("Failed to create run in DB: {}", e.what());
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
        
        pqxx::stream_to stream(W, "host_telemetry_archival");

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
        auto res = N.exec_params("SELECT status, inserted_rows, error FROM generation_runs WHERE run_id = $1", run_id);
        
        if (!res.empty()) {
            status.set_status(res[0][0].as<std::string>());
            status.set_inserted_rows(res[0][1].as<long>());
            if (!res[0][2].is_null()) {
                status.set_error(res[0][2].as<std::string>());
            }
        } else {
            status.set_status("NOT_FOUND");
        }
    } catch (const std::exception& e) {
        spdlog::error("DB Error in GetRunStatus for {}: {}", run_id, e.what());
        status.set_status("ERROR");
        status.set_error(e.what());
    }
    return status;
}

std::string DbClient::CreateModelRun(const std::string& dataset_id, const std::string& name) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        auto res = W.exec_params("INSERT INTO model_runs (dataset_id, name, status) "
                                 "VALUES ($1, $2, 'PENDING') RETURNING model_run_id",
                                 dataset_id, name);
        W.commit();
        if (!res.empty()) return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create model run: {}", e.what());
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
        auto res = N.exec_params("SELECT model_run_id, dataset_id, name, status, artifact_path, error, created_at, completed_at "
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
    }
    return "";
}

void DbClient::UpdateInferenceRunStatus(const std::string& inference_id, 
                                        const std::string& status, 
                                        int anomaly_count, 
                                        const nlohmann::json& details) {
    try {
        pqxx::connection C(conn_str_);
        pqxx::work W(C);
        W.exec_params("UPDATE inference_runs SET status=$1, anomaly_count=$2, details=$3::jsonb WHERE inference_id=$4",
                     status, anomaly_count, details.dump(), inference_id);
        W.commit();
    } catch (const std::exception& e) {
        spdlog::error("Failed to update inference run {}: {}", inference_id, e.what());
    }
}
