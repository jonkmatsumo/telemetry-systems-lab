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

        // ISO time strings to SQL timestamp conversion is handled by Postgres if string format is correct
        // But the proto has start_time_iso. 
        // We will insert simple placeholders for now, assuming ISO 8601 strings in config
        
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
        
        std::vector<std::string> columns = {
             "ingestion_time", "metric_timestamp", "host_id", "project_id", "region", 
             "cpu_usage", "memory_usage", "disk_utilization", "network_rx_rate", "network_tx_rate", 
             "labels", "run_id", "is_anomaly", "anomaly_type"
        };
        
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
        // In real system, we might retry or propagate error
        throw;
    }
}
