#include "detectors/pca_model.h"
#include "api_server.h"
#include "api_response_meta.h"
#include "api_debug.h"
#include "route_registry.h"
#include "time_resolution.h"
#include "training/pca_trainer.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <pqxx/pqxx>
#include "detectors/detector_a.h"
#include "preprocessing.h"
#include "detector_config.h"
#include "metrics.h"
#include "obs/metrics.h"
#include "obs/context.h"
#include "obs/error_codes.h"
#include "obs/http_log.h"

#include <uuid/uuid.h>

namespace telemetry {
namespace api {

std::string FormatServerTime() {
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now_t);
#else
    gmtime_r(&now_t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string GenerateUuid() {
    uuid_t out;
    uuid_generate(out);
    char str[37];
    uuid_unparse(out, str);
    return std::string(str);
}

std::string GetRequestId(const httplib::Request& req) {
    if (req.has_header("X-Request-ID")) {
        return req.get_header_value("X-Request-ID");
    }
    return GenerateUuid();
}

static const char* ClassifyTrainError(const std::string& msg) {
    if (msg.find("Not enough samples") != std::string::npos ||
        msg.find("No samples") != std::string::npos) {
        return telemetry::obs::kErrTrainNoData;
    }
    if (msg.find("Failed to open output path") != std::string::npos) {
        return telemetry::obs::kErrTrainArtifactWriteFailed;
    }
    return telemetry::obs::kErrInternal;
}

ApiServer::ApiServer(const std::string& grpc_target, const std::string& db_conn_str)
    : grpc_target_(grpc_target), db_conn_str_(db_conn_str)
{
    // Initialize gRPC Stub
    auto channel = grpc::CreateChannel(grpc_target, grpc::InsecureChannelCredentials());
    stub_ = telemetry::TelemetryService::NewStub(channel);

    // Initialize DB Client
    db_client_ = std::make_unique<DbClient>(db_conn_str);
    db_client_->ReconcileStaleJobs();
    
    // Ensure partitions for current and next month
    auto now = std::chrono::system_clock::now();
    db_client_->EnsurePartition(now);
    db_client_->EnsurePartition(now + std::chrono::hours(24 * 31));

    // Initialize Job Manager
    job_manager_ = std::make_unique<JobManager>();

    // Setup Routes
    svr_.Post("/datasets", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGenerateData(req, res);
    });

    svr_.Get("/datasets", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListDatasets(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDataset(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/summary", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetSummary(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/topk", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetTopK(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/timeseries", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetTimeSeries(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/histogram", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetHistogram(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/samples", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDatasetSamples(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/records/([0-9]+)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDatasetRecord(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/metrics/([a-zA-Z0-9_]+)/stats", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDatasetMetricStats(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/metrics/summary", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDatasetMetricsSummary(req, res);
    });

    svr_.Get("/datasets/([a-zA-Z0-9-]+)/models", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDatasetModels(req, res);
    });
    svr_.Get("/models/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetModelDetail(req, res);
    });

    svr_.Get("/models/([a-zA-Z0-9-]+)/datasets/scored", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetModelScoredDatasets(req, res);
    });

    svr_.Get("/scores", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetScores(req, res);
    });


    svr_.Post("/inference", [this](const httplib::Request& req, httplib::Response& res) {
        HandleInference(req, res);
    });

    svr_.Get("/inference_runs", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListInferenceRuns(req, res);
    });

    svr_.Get("/inference_runs/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetInferenceRun(req, res);
    });

    svr_.Post("/jobs/score_dataset", [this](const httplib::Request& req, httplib::Response& res) {
        HandleScoreDatasetJob(req, res);
    });

    svr_.Get("/jobs", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListJobs(req, res);
    });

    svr_.Get("/jobs/([a-zA-Z0-9-]+)/progress", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetJobProgress(req, res);
    });

    svr_.Get("/jobs/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetJobStatus(req, res);
    });

    svr_.Delete("/jobs/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        std::string job_id = req.matches[1];
        log.AddFields({{"score_job_id", job_id}});
        job_manager_->CancelJob(job_id);
        res.status = 200;
        res.set_content("{\"status\":\"CANCEL_REQUESTED\", \"job_id\":\"" + job_id + "\", \"request_id\":\"" + rid + "\"}", "application/json");
    });

    svr_.Get("/models/([a-zA-Z0-9-]+)/eval", [this](const httplib::Request& req, httplib::Response& res) {
        HandleModelEval(req, res);
    });

    svr_.Get("/models/([a-zA-Z0-9-]+)/error_distribution", [this](const httplib::Request& req, httplib::Response& res) {
        HandleModelErrorDistribution(req, res);
    });

    svr_.Get("/healthz", [](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        res.status = 200;
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });

    svr_.Get("/readyz", [this](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        try {
            pqxx::connection C(db_conn_str_);
            res.status = 200;
            res.set_content("{\"status\":\"READY\"}", "application/json");
        } catch (const std::exception& e) {
            log.RecordError(telemetry::obs::kErrDbConnectFailed, e.what(), 503);
            res.status = 503;
            res.set_content("{\"status\":\"UNREADY\", \"reason\":\"DB_CONNECTION_FAILED\"}", "application/json");
        }
    });

    svr_.Get("/metrics", [](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        res.status = 200;
        res.set_content(telemetry::metrics::MetricsRegistry::Instance().ToPrometheus(), "text/plain");
    });

    svr_.Get("/schema/metrics", [this](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        nlohmann::json resp;
        resp["metrics"] = {
            {{"key", "cpu_usage"}, {"label", "CPU Usage"}, {"type", "numeric"}, {"unit", "%"}, {"description", "Percentage of CPU time used across all cores."}},
            {{"key", "memory_usage"}, {"label", "Memory Usage"}, {"type", "numeric"}, {"unit", "%"}, {"description", "Percentage of physical RAM currently occupied."}},
            {{"key", "disk_utilization"}, {"label", "Disk Utilization"}, {"type", "numeric"}, {"unit", "%"}, {"description", "Percentage of disk throughput capacity used."}},
            {{"key", "network_rx_rate"}, {"label", "Network RX Rate"}, {"type", "numeric"}, {"unit", "Mbps"}, {"description", "Inbound network traffic rate."}},
            {{"key", "network_tx_rate"}, {"label", "Network TX Rate"}, {"type", "numeric"}, {"unit", "Mbps"}, {"description", "Outbound network traffic rate."}}
        };
        SendJson(res, resp, 200, rid);
    });

    // Training routes
    svr_.Post("/train", [this](const httplib::Request& req, httplib::Response& res) {
        HandleTrainModel(req, res);
    });

    svr_.Get("/train/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetTrainStatus(req, res);
    });

    // Model listing route
    svr_.Get("/models", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListModels(req, res);
    });

    // Serve Static Web UI
    svr_.set_mount_point("/", "./www");

    // CORS Support
    svr_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
}

ApiServer::~ApiServer() {}

void ApiServer::Start(const std::string& host, int port) {
    ValidateRoutes();
    spdlog::info("HTTP API Server listening on {}:{}", host, port);
    svr_.listen(host.c_str(), port);
}

void ApiServer::Stop() {
    svr_.stop();
}

void ApiServer::HandleGenerateData(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    try {
        auto j = nlohmann::json::parse(req.body);
        int host_count = j.value("host_count", 5);
        std::string run_id = j.value("run_id", ""); // Optional, will be generated if empty

        grpc::ClientContext context;
        telemetry::GenerateRequest g_req;
        g_req.set_host_count(host_count);
        g_req.set_tier("USER_UI");
        g_req.set_request_id(rid);
        if (!run_id.empty()) {
            // NOTE: Proto doesn't support setting run_id yet. 
            // We ignore it for now or could update proto later.
            spdlog::warn("Ignoring user-provided run_id: {}", run_id);
        }

        telemetry::GenerateResponse g_res;
        grpc::Status status = stub_->GenerateTelemetry(&context, g_req, &g_res);

        if (status.ok()) {
            nlohmann::json resp;
            resp["run_id"] = g_res.run_id();
            resp["status"] = "PENDING";
            log.AddFields({{"dataset_id", g_res.run_id()}});
            SendJson(res, resp, 202, rid);
        } else {
            log.RecordError(telemetry::obs::kErrHttpGrpcError, status.error_message(), 500);
            SendError(res, "gRPC Error: " + status.error_message(), 500, telemetry::obs::kErrHttpGrpcError, rid);
        }
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrHttpBadRequest, e.what(), 400);
        SendError(res, std::string("Error: ") + e.what(), 400, telemetry::obs::kErrHttpBadRequest, rid);
    }
}

void ApiServer::HandleListDatasets(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    std::string status = GetStrParam(req, "status");
    std::string created_from = GetStrParam(req, "created_from");
    std::string created_to = GetStrParam(req, "created_to");
    try {
        auto runs = db_client_->ListGenerationRuns(limit, offset, status, created_from, created_to);
        nlohmann::json resp;
        resp["items"] = runs;
        resp["limit"] = limit;
        resp["offset"] = offset;
        SendJson(res, resp, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetDataset(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    try {
        auto detail = db_client_->GetDatasetDetail(run_id);
        if (!detail.empty()) {
            SendJson(res, detail, 200, rid);
            return;
        }
    } catch (const std::exception& e) {
        spdlog::warn("DB Detail check failed, falling back to gRPC: {}", e.what());
    }

    grpc::ClientContext context;
    telemetry::GetRunRequest g_req;
    g_req.set_run_id(run_id);

    telemetry::RunStatus g_res;
    grpc::Status status = stub_->GetRun(&context, g_req, &g_res);

    if (status.ok()) {
        nlohmann::json resp;
        resp["run_id"] = g_res.run_id();
        resp["status"] = g_res.status();
        resp["rows_inserted"] = g_res.inserted_rows();
        resp["error"] = g_res.error();
        SendJson(res, resp, 200, rid);
    } else {
        log.RecordError(telemetry::obs::kErrHttpNotFound, status.error_message(), 404);
        SendError(res, "gRPC Error: " + status.error_message(), 404, telemetry::obs::kErrHttpNotFound, rid);
    }
}

void ApiServer::HandleDatasetSummary(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    int topk = GetIntParam(req, "topk", 5);
    bool debug = GetStrParam(req, "debug") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto summary = db_client_->GetDatasetSummary(run_id, topk);
        auto end = std::chrono::steady_clock::now();
        if (summary.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Dataset not found", 404);
            SendError(res, "Dataset not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        summary["meta"]["duration_ms"] = duration_ms;
        summary["meta"]["rows_scanned"] = nullptr;
        summary["meta"]["rows_returned"] = 1;
        summary["meta"]["cache_hit"] = false;
        summary["meta"]["request_id"] = rid;
        if (debug) {
            long row_count = summary.value("row_count", 0L);
            summary["debug"] = BuildDebugMeta(duration_ms, row_count);
        }
        summary["meta"]["server_time"] = FormatServerTime();
        SendJson(res, summary, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleDatasetTopK(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    std::string column = GetStrParam(req, "column");
    int k = GetIntParam(req, "k", 10);
    std::string region = GetStrParam(req, "region");
    std::string is_anomaly = GetStrParam(req, "is_anomaly");
    std::string anomaly_type = GetStrParam(req, "anomaly_type");
    std::string start_time = GetStrParam(req, "start_time");
    std::string end_time = GetStrParam(req, "end_time");

    std::unordered_map<std::string, std::string> allowed = {
        {"region", "region"},
        {"project_id", "project_id"},
        {"host_id", "host_id"},
        {"anomaly_type", "anomaly_type"}
    };
    if (allowed.find(column) == allowed.end()) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "Invalid column", 400);
        SendError(res, "Invalid column", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
        return;
    }
    bool debug = GetStrParam(req, "debug") == "true";
    bool include_total = GetStrParam(req, "include_total_distinct") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto data_obj = db_client_->GetTopK(run_id, allowed[column], k, region, is_anomaly, anomaly_type, start_time, end_time, include_total);
        auto end = std::chrono::steady_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        nlohmann::json resp;
        auto& items = data_obj["items"];
        resp["items"] = items;
        
        std::optional<long> total_distinct = std::nullopt;
        if (data_obj.contains("total_distinct")) {
            total_distinct = data_obj["total_distinct"].get<long>();
        }

        bool truncated = data_obj.value("truncated", false);
        if (!data_obj.contains("truncated")) {
             truncated = telemetry::api::IsTruncated(static_cast<int>(items.size()), k, total_distinct);
        }

        resp["meta"] = telemetry::api::BuildResponseMeta(
            k,
            static_cast<int>(items.size()),
            truncated,
            total_distinct,
            "top_k_limit");
        resp["meta"]["start_time"] = start_time;
        resp["meta"]["end_time"] = end_time;
        resp["meta"]["server_time"] = FormatServerTime();
        resp["meta"]["duration_ms"] = duration_ms;
        resp["meta"]["rows_scanned"] = nullptr;
        resp["meta"]["rows_returned"] = static_cast<int>(items.size());
        resp["meta"]["cache_hit"] = false;
        resp["meta"]["request_id"] = rid;

        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            nlohmann::json resolved;
            resolved["column"] = allowed[column];
            resp["debug"] = BuildDebugMeta(duration_ms, static_cast<long>(items.size()), resolved);
        }
        SendJson(res, resp, 200, rid);
    } catch (const std::invalid_argument& e) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, e.what(), 400);
        SendError(res, e.what(), 400, telemetry::obs::kErrHttpInvalidArgument, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleDatasetTimeSeries(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    std::string metrics_param = GetStrParam(req, "metrics");
    // ... (rest of parsing)
    std::string aggs_param = GetStrParam(req, "aggs");
    std::string bucket = GetStrParam(req, "bucket");
    std::string region = GetStrParam(req, "region");
    std::string is_anomaly = GetStrParam(req, "is_anomaly");
    std::string anomaly_type = GetStrParam(req, "anomaly_type");
    std::string compare_mode = GetStrParam(req, "compare_mode");
    std::string start_time = GetStrParam(req, "start_time");
    std::string end_time = GetStrParam(req, "end_time");

    std::vector<std::string> metrics;
    std::stringstream ms(metrics_param);
    std::string token;
    while (std::getline(ms, token, ',')) {
        if (!token.empty()) metrics.push_back(token);
    }
    if (metrics.empty()) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "metrics required", 400);
        SendError(res, "metrics required", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
        return;
    }
    if (!compare_mode.empty() && compare_mode != "previous_period") {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "invalid compare_mode", 400);
        SendError(res, "compare_mode must be previous_period", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
        return;
    }

    std::vector<std::string> aggs;
    std::stringstream as(aggs_param.empty() ? "mean" : aggs_param);
    while (std::getline(as, token, ',')) {
        if (!token.empty()) aggs.push_back(token);
    }

    int bucket_seconds = 3600;
    if (bucket == "1m") bucket_seconds = 60;
    else if (bucket == "5m") bucket_seconds = 300;
    else if (bucket == "15m") bucket_seconds = 900;
    else if (bucket == "1h") bucket_seconds = 3600;
    else if (bucket == "6h") bucket_seconds = 21600;
    else if (bucket == "1d") bucket_seconds = 86400;
    else if (bucket == "7d") bucket_seconds = 604800;
    else if (bucket.empty() || bucket == "auto") bucket_seconds = telemetry::api::SelectBucketSeconds(start_time, end_time);

    bool debug = GetStrParam(req, "debug") == "true";
    std::optional<std::pair<std::string, std::string>> baseline_window;
    if (compare_mode == "previous_period") {
        baseline_window = telemetry::api::PreviousPeriodWindow(start_time, end_time);
        if (!baseline_window.has_value()) {
            log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "compare_mode requires start_time and end_time", 400);
            SendError(res, "compare_mode requires start_time and end_time", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
            return;
        }
    }
    try {
        auto start = std::chrono::steady_clock::now();
        auto data = db_client_->GetTimeSeries(run_id, metrics, aggs, bucket_seconds, region, is_anomaly, anomaly_type, start_time, end_time);
        nlohmann::json baseline;
        if (baseline_window.has_value()) {
            baseline = db_client_->GetTimeSeries(
                run_id,
                metrics,
                aggs,
                bucket_seconds,
                region,
                is_anomaly,
                anomaly_type,
                baseline_window->first,
                baseline_window->second);
        }
        auto end = std::chrono::steady_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        nlohmann::json resp;
        resp["items"] = data;
        if (baseline_window.has_value()) {
            resp["baseline"] = baseline;
        }
        resp["bucket_seconds"] = bucket_seconds;
        resp["meta"]["start_time"] = start_time;
        resp["meta"]["end_time"] = end_time;
        resp["meta"]["bucket_seconds"] = bucket_seconds;
        resp["meta"]["resolution"] = telemetry::api::BucketLabel(bucket_seconds);
        if (baseline_window.has_value()) {
            resp["meta"]["compare_mode"] = compare_mode;
            resp["meta"]["baseline_start_time"] = baseline_window->first;
            resp["meta"]["baseline_end_time"] = baseline_window->second;
        }
        resp["meta"]["server_time"] = FormatServerTime();
        resp["meta"]["duration_ms"] = duration_ms;
        resp["meta"]["rows_scanned"] = nullptr;
        resp["meta"]["rows_returned"] = static_cast<int>(data.size());
        resp["meta"]["cache_hit"] = false;
        resp["meta"]["request_id"] = rid;
        if (debug) {
            nlohmann::json resolved;
            resolved["metrics"] = metrics;
            resolved["aggs"] = aggs;
            resolved["bucket_seconds"] = bucket_seconds;
            resp["debug"] = BuildDebugMeta(duration_ms, static_cast<long>(data.size()), resolved);
        }
        SendJson(res, resp, 200, rid);
    } catch (const std::invalid_argument& e) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, e.what(), 400);
        SendError(res, e.what(), 400, telemetry::obs::kErrHttpInvalidArgument, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleDatasetHistogram(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    std::string metric = GetStrParam(req, "metric");
    if (metric.empty()) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "metric required", 400);
        SendError(res, "metric required", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
        return;
    }
    int bins = GetIntParam(req, "bins", 40);
    double min_val = GetDoubleParam(req, "min", 0.0);
    double max_val = GetDoubleParam(req, "max", 0.0);
    std::string range = GetStrParam(req, "range");
    if (range == "minmax") {
        min_val = 0.0;
        max_val = 0.0;
    }
    std::string region = GetStrParam(req, "region");
    std::string is_anomaly = GetStrParam(req, "is_anomaly");
    std::string anomaly_type = GetStrParam(req, "anomaly_type");
    std::string start_time = GetStrParam(req, "start_time");
    std::string end_time = GetStrParam(req, "end_time");

    bool debug = GetStrParam(req, "debug") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto data = db_client_->GetHistogram(run_id, metric, bins, min_val, max_val, region, is_anomaly, anomaly_type, start_time, end_time);
        auto end = std::chrono::steady_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        int requested_bins = data.value("requested_bins", bins);
        int returned_bins = data.value("counts", nlohmann::json::array()).size();
        bool truncated = requested_bins > returned_bins;
        data["meta"] = telemetry::api::BuildResponseMeta(
            requested_bins,
            returned_bins,
            truncated,
            std::nullopt,
            truncated ? "max_bins_cap" : "histogram_bins",
            requested_bins,
            returned_bins);
        data["meta"]["start_time"] = start_time;
        data["meta"]["end_time"] = end_time;
        data["meta"]["server_time"] = FormatServerTime();
        data["meta"]["duration_ms"] = duration_ms;
        data["meta"]["rows_scanned"] = nullptr;
        data["meta"]["rows_returned"] = returned_bins;
        data["meta"]["cache_hit"] = false;
        data["meta"]["request_id"] = rid;
        
        if (debug) {
            long row_count = data.value("counts", nlohmann::json::array()).size();
            nlohmann::json resolved;
            resolved["metric"] = metric;
            resolved["bins"] = bins;
            resolved["min"] = min_val;
            resolved["max"] = max_val;
            data["debug"] = BuildDebugMeta(duration_ms, row_count, resolved);
        }
        SendJson(res, data, 200, rid);
    } catch (const std::invalid_argument& e) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, e.what(), 400);
        SendError(res, e.what(), 400, telemetry::obs::kErrHttpInvalidArgument, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetDatasetSamples(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    int limit = GetIntParam(req, "limit", 20);
    int offset = GetIntParam(req, "offset", 0);
    
    std::string start_time = GetStrParam(req, "start_time");
    std::string end_time = GetStrParam(req, "end_time");
    std::string is_anomaly = GetStrParam(req, "is_anomaly");
    std::string anomaly_type = GetStrParam(req, "anomaly_type");
    std::string host_id = GetStrParam(req, "host_id");
    std::string region = GetStrParam(req, "region");
    std::string sort_by = GetStrParam(req, "sort_by");
    std::string sort_order = GetStrParam(req, "sort_order");
    std::string anchor_time = GetStrParam(req, "anchor_time");

    try {
        auto data = db_client_->SearchDatasetRecords(
            run_id,
            limit,
            offset,
            start_time,
            end_time,
            is_anomaly,
            anomaly_type,
            host_id,
            region,
            sort_by,
            sort_order,
            anchor_time);
        SendJson(res, data, 200, rid);
    } catch (const std::invalid_argument& e) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, e.what(), 400);
        SendError(res, e.what(), 400, telemetry::obs::kErrHttpInvalidArgument, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetDatasetRecord(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    long record_id = std::stol(req.matches[2]);
    try {
        auto data = db_client_->GetDatasetRecord(run_id, record_id);
        if (data.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Record not found", 404);
            SendError(res, "Record not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }
        SendJson(res, data, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetDatasetMetricStats(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    std::string metric = req.matches[2];
    try {
        auto data = db_client_->GetMetricStats(run_id, metric);
        SendJson(res, data, 200, rid);
    } catch (const std::invalid_argument& e) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, e.what(), 400);
        SendError(res, e.what(), 400, telemetry::obs::kErrHttpInvalidArgument, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetDatasetMetricsSummary(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    try {
        auto data = db_client_->GetDatasetMetricsSummary(run_id);
        SendJson(res, data, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetDatasetModels(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string run_id = req.matches[1];
    log.AddFields({{"dataset_id", run_id}});
    try {
        auto data = db_client_->GetModelsForDataset(run_id);
        SendJson(res, data, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleTrainModel(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    try {
        auto j = nlohmann::json::parse(req.body);
        std::string dataset_id = j.at("dataset_id");
        std::string name = j.value("name", "pca_default");
        log.AddFields({{"dataset_id", dataset_id}});

        // 1. Create DB entry
        std::string model_run_id = db_client_->CreateModelRun(dataset_id, name, rid);
        if (model_run_id.empty()) {
            log.RecordError(telemetry::obs::kErrDbInsertFailed, "Failed to create model run in DB", 500);
            SendError(res, "Failed to create model run in DB", 500, telemetry::obs::kErrDbInsertFailed, rid);
            return;
        }
        log.AddFields({{"model_run_id", model_run_id}});

        // 2. Spawn training via JobManager
        job_manager_->StartJob("train-" + model_run_id, rid, [this, model_run_id, dataset_id, rid](const std::atomic<bool>* stop_flag) {
            telemetry::obs::Context ctx;
            ctx.request_id = rid;
            ctx.dataset_id = dataset_id;
            ctx.model_run_id = model_run_id;
            telemetry::obs::ScopedContext scope(ctx);
            telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "train_start", "trainer",
                                     {{"request_id", rid}, {"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
            auto train_start = std::chrono::steady_clock::now();
            spdlog::info("Training started for model {} (req_id: {})", model_run_id, rid);
            db_client_->UpdateModelRunStatus(model_run_id, "RUNNING");

            std::string output_dir = "artifacts/pca/" + model_run_id;
            std::string output_path = output_dir + "/model.json";

            try {
                std::filesystem::create_directories(output_dir);
                auto artifact = telemetry::training::TrainPcaFromDb(db_conn_str_, dataset_id, 3, 99.5);
                telemetry::training::WriteArtifactJson(artifact, output_path);

                spdlog::info("Training successful for model {}", model_run_id);
                db_client_->UpdateModelRunStatus(model_run_id, "COMPLETED", output_path);
                auto train_end = std::chrono::steady_clock::now();
                double duration_ms = std::chrono::duration<double, std::milli>(train_end - train_start).count();
                telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "train_end", "trainer",
                                         {{"request_id", rid},
                                          {"dataset_id", dataset_id},
                                          {"model_run_id", model_run_id},
                                          {"artifact_path", output_path},
                                          {"duration_ms", duration_ms}});
            } catch (const std::exception& e) {
                auto train_end = std::chrono::steady_clock::now();
                double duration_ms = std::chrono::duration<double, std::milli>(train_end - train_start).count();
                const char* error_code = ClassifyTrainError(e.what());
                telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "train_error", "trainer",
                                         {{"request_id", rid},
                                          {"dataset_id", dataset_id},
                                          {"model_run_id", model_run_id},
                                          {"error_code", error_code},
                                          {"error", e.what()},
                                          {"duration_ms", duration_ms}});
                spdlog::error("Training failed for model {}: {}", model_run_id, e.what());
                db_client_->UpdateModelRunStatus(model_run_id, "FAILED", "", e.what());
                throw; // JobManager will catch and log it too
            }
        });

        nlohmann::json resp;
        resp["model_run_id"] = model_run_id;
        resp["status"] = "PENDING";
        SendJson(res, resp, 202, rid);
    } catch (const std::exception& e) {
        std::string err = e.what();
        if (err.find("Job queue full") != std::string::npos) {
            log.RecordError(telemetry::obs::kErrHttpResourceExhausted, err, 503);
            SendError(res, err, 503, telemetry::obs::kErrHttpResourceExhausted, rid);
        } else {
            log.RecordError(telemetry::obs::kErrHttpBadRequest, err, 400);
            SendError(res, std::string("Error: ") + err, 400, telemetry::obs::kErrHttpBadRequest, rid);
        }
    }
}

void ApiServer::HandleGetTrainStatus(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string model_run_id = req.matches[1];
    log.AddFields({{"model_run_id", model_run_id}});
    auto j = db_client_->GetModelRun(model_run_id);
    if (j.empty()) {
        log.RecordError(telemetry::obs::kErrHttpNotFound, "Model run not found", 404);
        SendError(res, "Model run not found", 404, telemetry::obs::kErrHttpNotFound, rid);
    } else {
        SendJson(res, j, 200, rid);
    }
}

void ApiServer::HandleListModels(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    std::string status = GetStrParam(req, "status");
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string created_from = GetStrParam(req, "created_from");
    std::string created_to = GetStrParam(req, "created_to");
    try {
        auto models = db_client_->ListModelRuns(limit, offset, status, dataset_id, created_from, created_to);
        nlohmann::json resp;
        resp["items"] = models;
        resp["limit"] = limit;
        resp["offset"] = offset;
        SendJson(res, resp, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetModelDetail(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string model_run_id = req.matches[1];
    log.AddFields({{"model_run_id", model_run_id}});
    try {
        auto j = db_client_->GetModelRun(model_run_id);
        if (j.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Model run not found", 404);
            SendError(res, "Model run not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }
        std::string artifact_path = j.value("artifact_path", "");
        if (!artifact_path.empty()) {
            try {
                std::ifstream f(artifact_path);
                if (f.is_open()) {
                    nlohmann::json artifact;
                    f >> artifact;
                    j["artifact"]["thresholds"] = artifact.value("thresholds", nlohmann::json::object());
                    j["artifact"]["model"]["n_components"] = artifact["model"].value("n_components", 0);
                    j["artifact"]["model"]["features"] = artifact["meta"].value("features", nlohmann::json::array());
                }
            } catch (const std::exception& e) {
                j["artifact_error"] = e.what();
            }
        }
        SendJson(res, j, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetModelScoredDatasets(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string model_run_id = req.matches[1];
    log.AddFields({{"model_run_id", model_run_id}});
    try {
        auto data = db_client_->GetScoredDatasetsForModel(model_run_id);
        SendJson(res, data, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetScores(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string model_run_id = GetStrParam(req, "model_run_id");
    log.AddFields({{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
    telemetry::obs::Context ctx;
    ctx.request_id = rid;
    ctx.dataset_id = dataset_id;
    ctx.model_run_id = model_run_id;
    telemetry::obs::ScopedContext scope(ctx);
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    bool only_anomalies = GetStrParam(req, "only_anomalies") == "true";
    double min_score = GetDoubleParam(req, "min_score", 0.0);
    double max_score = GetDoubleParam(req, "max_score", 0.0);

    if (dataset_id.empty() || model_run_id.empty()) {
        log.RecordError(telemetry::obs::kErrHttpBadRequest, "dataset_id and model_run_id required", 400);
        SendError(res, "dataset_id and model_run_id required", 400, telemetry::obs::kErrHttpBadRequest, rid);
        return;
    }

    try {
        auto data = db_client_->GetScores(dataset_id, model_run_id, limit, offset, only_anomalies, min_score, max_score);
        SendJson(res, data, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleInference(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    try {
        auto start = std::chrono::steady_clock::now();
        auto j = nlohmann::json::parse(req.body);
        std::string model_run_id = j.at("model_run_id");
        auto samples = j.at("samples");
        log.AddFields({{"model_run_id", model_run_id}});
        telemetry::obs::Context ctx;
        ctx.request_id = rid;
        ctx.model_run_id = model_run_id;
        telemetry::obs::ScopedContext scope(ctx);
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "infer_start", "model",
                                 {{"request_id", rid}, {"model_run_id", model_run_id}});

        // 1. Get Model Info
        auto model_info = db_client_->GetModelRun(model_run_id);
        if (model_info.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Model not found", 404);
            SendError(res, "Model not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }

        std::string artifact_path = model_info.value("artifact_path", "");
        if (artifact_path.empty()) {
            log.RecordError(telemetry::obs::kErrHttpBadRequest, "Model is not yet complete or has no artifact", 400);
            SendError(res, "Model is not yet complete or has no artifact", 400, telemetry::obs::kErrHttpBadRequest, rid);
            return;
        }

        // 2. Load Model
        telemetry::anomaly::PcaModel pca;
        try {
            pca.Load(artifact_path);
        } catch (const std::exception& e) {
            log.RecordError(telemetry::obs::kErrModelLoadFailed, e.what(), 500);
            SendError(res, "Failed to load PCA model artifact: " + std::string(e.what()), 500, telemetry::obs::kErrModelLoadFailed, rid);
            return;
        }

        // 3. Process Samples
        std::string inference_id = db_client_->CreateInferenceRun(model_run_id);
        if (!inference_id.empty()) {
            log.AddFields({{"inference_run_id", inference_id}});
            ctx.inference_run_id = inference_id;
            telemetry::obs::UpdateContext(ctx);
        }
        int anomaly_count = 0;
        nlohmann::json results = nlohmann::json::array();
        for (const auto& s : samples) {
            telemetry::anomaly::FeatureVector v;
            v.data[0] = s.value("cpu_usage", 0.0);
            v.data[1] = s.value("memory_usage", 0.0);
            v.data[2] = s.value("disk_utilization", 0.0);
            v.data[3] = s.value("network_rx_rate", 0.0);
            v.data[4] = s.value("network_tx_rate", 0.0);

            auto score = pca.Score(v);
            
            nlohmann::json r;
            r["is_anomaly"] = score.is_anomaly;
            r["score"] = score.reconstruction_error;
            results.push_back(r);
            if (score.is_anomaly) anomaly_count++;
        }

        auto end = std::chrono::steady_clock::now();
        double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (!inference_id.empty()) {
            db_client_->UpdateInferenceRunStatus(inference_id, "COMPLETED", anomaly_count, results, latency_ms);
        }
        telemetry::obs::EmitHistogram("infer_duration_ms", latency_ms, "ms", "model",
                                      {{"model_run_id", model_run_id}},
                                      {{"inference_run_id", inference_id}});
        telemetry::obs::EmitCounter("infer_rows_scored", static_cast<long>(results.size()), "rows", "model",
                                    {{"model_run_id", model_run_id}},
                                    {{"inference_run_id", inference_id}});
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "infer_end", "model",
                                 {{"request_id", rid},
                                  {"model_run_id", model_run_id},
                                  {"inference_run_id", inference_id},
                                  {"rows", results.size()},
                                  {"duration_ms", latency_ms}});

        nlohmann::json resp;
        resp["results"] = results;
        resp["model_run_id"] = model_run_id;
        resp["inference_id"] = inference_id;
        resp["inference_run_id"] = inference_id;
        resp["anomaly_count"] = anomaly_count;
        SendJson(res, resp, 200, rid);

    } catch (const std::exception& e) {
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "infer_error", "model",
                                 {{"request_id", rid}, {"error_code", telemetry::obs::kErrHttpBadRequest}, {"error", e.what()}});
        log.RecordError(telemetry::obs::kErrHttpBadRequest, e.what(), 400);
        SendError(res, std::string("Error: ") + e.what(), 400, telemetry::obs::kErrHttpBadRequest, rid);
    }
}

void ApiServer::HandleListInferenceRuns(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string model_run_id = GetStrParam(req, "model_run_id");
    log.AddFields({{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    std::string status = GetStrParam(req, "status");
    std::string created_from = GetStrParam(req, "created_from");
    std::string created_to = GetStrParam(req, "created_to");
    try {
        auto runs = db_client_->ListInferenceRuns(dataset_id, model_run_id, limit, offset, status, created_from, created_to);
        nlohmann::json resp;
        resp["items"] = runs;
        resp["limit"] = limit;
        resp["offset"] = offset;
        SendJson(res, resp, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetInferenceRun(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string inference_id = req.matches[1];
    log.AddFields({{"inference_run_id", inference_id}});
    try {
        auto j = db_client_->GetInferenceRun(inference_id);
        if (j.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Inference run not found", 404);
            SendError(res, "Inference run not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }
        SendJson(res, j, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleListJobs(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    std::string status = GetStrParam(req, "status");
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string model_run_id = GetStrParam(req, "model_run_id");
    log.AddFields({{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
    std::string created_from = GetStrParam(req, "created_from");
    std::string created_to = GetStrParam(req, "created_to");
    try {
        auto jobs = db_client_->ListScoreJobs(limit, offset, status, dataset_id, model_run_id, created_from, created_to);
        nlohmann::json resp;
        resp["items"] = jobs;
        resp["limit"] = limit;
        resp["offset"] = offset;
        SendJson(res, resp, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleScoreDatasetJob(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    try {
        auto j = nlohmann::json::parse(req.body);
        std::string dataset_id = j.at("dataset_id");
        std::string model_run_id = j.at("model_run_id");
        log.AddFields({{"dataset_id", dataset_id}, {"model_run_id", model_run_id}});

        std::string job_id = db_client_->CreateScoreJob(dataset_id, model_run_id, rid);
        if (job_id.empty()) {
            log.RecordError(telemetry::obs::kErrDbInsertFailed, "Failed to create job", 500);
            SendError(res, "Failed to create job", 500, telemetry::obs::kErrDbInsertFailed, rid);
            return;
        }
        log.AddFields({{"score_job_id", job_id}});

        auto job = db_client_->GetScoreJob(job_id);
        if (job.empty()) {
            log.RecordError(telemetry::obs::kErrDbQueryFailed, "Failed to load job status", 500);
            SendError(res, "Failed to load job status", 500, telemetry::obs::kErrDbQueryFailed, rid);
            return;
        }

        std::string status = job.value("status", "");
        long total_rows = job.value("total_rows", 0L);
        long processed_rows = job.value("processed_rows", 0L);
        long last_record_id = job.value("last_record_id", 0L);

        if (status == "RUNNING" || status == "COMPLETED") {
            nlohmann::json resp;
            resp["job_id"] = job_id;
            resp["score_job_id"] = job_id;
            resp["status"] = status;
            resp["total_rows"] = total_rows;
            resp["processed_rows"] = processed_rows;
            resp["last_record_id"] = last_record_id;
            SendJson(res, resp, 200, rid);
            return;
        }

        job_manager_->StartJob("score-" + job_id, rid, [this, dataset_id, model_run_id, job_id, rid](const std::atomic<bool>* stop_flag) {
            telemetry::obs::Context ctx;
            ctx.request_id = rid;
            ctx.dataset_id = dataset_id;
            ctx.model_run_id = model_run_id;
            ctx.score_job_id = job_id;
            telemetry::obs::ScopedContext scope(ctx);
            telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "score_job_start", "model",
                                     {{"request_id", rid},
                                      {"dataset_id", dataset_id},
                                      {"model_run_id", model_run_id},
                                      {"score_job_id", job_id}});
            auto job_start = std::chrono::steady_clock::now();
            DbClient local_db(db_conn_str_);
            try {
                auto job_info = local_db.GetScoreJob(job_id);
                long total = job_info.value("total_rows", 0L);
                long processed = job_info.value("processed_rows", 0L);
                long last_record = job_info.value("last_record_id", 0L);

                pqxx::connection C(db_conn_str_);
                pqxx::nontransaction N(C);
                auto count_res = N.exec_params(
                    "SELECT COUNT(*) FROM host_telemetry_archival WHERE run_id = $1",
                    dataset_id);
                total = count_res.empty() ? 0 : count_res[0][0].as<long>();
                local_db.UpdateScoreJob(job_id, "RUNNING", total, processed, last_record);

                auto model_info = local_db.GetModelRun(model_run_id);
                std::string artifact_path = model_info.value("artifact_path", "");
                if (artifact_path.empty()) {
                    throw std::runtime_error("Model artifact path missing");
                }
                telemetry::anomaly::PcaModel model;
                model.Load(artifact_path);

                const int batch = 5000;
                while (!stop_flag->load()) {
                    auto rows = local_db.FetchScoringRowsAfterRecord(dataset_id, last_record, batch);
                    if (rows.empty()) break;
                    std::vector<std::pair<long, std::pair<double, bool>>> scores;
                    scores.reserve(rows.size());
                    for (const auto& r : rows) {
                        telemetry::anomaly::FeatureVector v;
                        v.data[0] = r.cpu;
                        v.data[1] = r.mem;
                        v.data[2] = r.disk;
                        v.data[3] = r.rx;
                        v.data[4] = r.tx;
                        auto score = model.Score(v);
                        scores.emplace_back(r.record_id, std::make_pair(score.reconstruction_error, score.is_anomaly));
                    }
                    local_db.InsertDatasetScores(dataset_id, model_run_id, scores);
                    processed += static_cast<long>(rows.size());
                    last_record = rows.back().record_id;
                    local_db.UpdateScoreJob(job_id, "RUNNING", total, processed, last_record);
                }
                
                if (stop_flag->load()) {
                    spdlog::info("Job {} cancelled by request.", job_id);
                    local_db.UpdateScoreJob(job_id, "CANCELLED", total, processed, last_record);
                    auto job_end = std::chrono::steady_clock::now();
                    double duration_ms = std::chrono::duration<double, std::milli>(job_end - job_start).count();
                    telemetry::obs::LogEvent(telemetry::obs::LogLevel::Warn, "score_job_end", "model",
                                             {{"request_id", rid},
                                              {"dataset_id", dataset_id},
                                              {"model_run_id", model_run_id},
                                              {"score_job_id", job_id},
                                              {"status", "CANCELLED"},
                                              {"duration_ms", duration_ms}});
                } else {
                    local_db.UpdateScoreJob(job_id, "COMPLETED", total, processed, last_record);
                    auto job_end = std::chrono::steady_clock::now();
                    double duration_ms = std::chrono::duration<double, std::milli>(job_end - job_start).count();
                    telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "score_job_end", "model",
                                             {{"request_id", rid},
                                              {"dataset_id", dataset_id},
                                              {"model_run_id", model_run_id},
                                              {"score_job_id", job_id},
                                              {"status", "COMPLETED"},
                                              {"duration_ms", duration_ms}});
                }
            } catch (const std::exception& e) {
                auto job_info = local_db.GetScoreJob(job_id);
                long total = job_info.value("total_rows", 0L);
                long processed = job_info.value("processed_rows", 0L);
                long last_record = job_info.value("last_record_id", 0L);
                local_db.UpdateScoreJob(job_id, "FAILED", total, processed, last_record, e.what());
                auto job_end = std::chrono::steady_clock::now();
                double duration_ms = std::chrono::duration<double, std::milli>(job_end - job_start).count();
                telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "score_job_error", "model",
                                         {{"request_id", rid},
                                          {"dataset_id", dataset_id},
                                          {"model_run_id", model_run_id},
                                          {"score_job_id", job_id},
                                          {"error_code", telemetry::obs::kErrInferScoreFailed},
                                          {"error", e.what()},
                                          {"duration_ms", duration_ms}});
                throw;
            }
        });

        nlohmann::json resp;
        resp["job_id"] = job_id;
        resp["score_job_id"] = job_id;
        resp["status"] = "RUNNING";
        SendJson(res, resp, 202, rid);
    } catch (const std::exception& e) {
        std::string err = e.what();
        if (err.find("Job queue full") != std::string::npos) {
            log.RecordError(telemetry::obs::kErrHttpResourceExhausted, err, 503);
            SendError(res, err, 503, telemetry::obs::kErrHttpResourceExhausted, rid);
        } else {
            log.RecordError(telemetry::obs::kErrHttpBadRequest, err, 400);
            SendError(res, std::string("Error: ") + err, 400, telemetry::obs::kErrHttpBadRequest, rid);
        }
    }
}

void ApiServer::HandleGetJobStatus(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string job_id = req.matches[1];
    log.AddFields({{"score_job_id", job_id}});
    try {
        auto j = db_client_->GetScoreJob(job_id);
        if (j.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Job not found", 404);
            SendError(res, "Job not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }
        SendJson(res, j, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleGetJobProgress(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string job_id = req.matches[1];
    log.AddFields({{"score_job_id", job_id}});
    try {
        auto j = db_client_->GetScoreJob(job_id);
        if (j.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Job not found", 404);
            SendError(res, "Job not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }
        SendJson(res, j, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleModelEval(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string model_run_id = req.matches[1];
    std::string dataset_id = GetStrParam(req, "dataset_id");
    log.AddFields({{"model_run_id", model_run_id}, {"dataset_id", dataset_id}});
    int points = GetIntParam(req, "points", 50);
    int max_samples = GetIntParam(req, "max_samples", 20000);
    bool debug = GetStrParam(req, "debug") == "true";
    if (dataset_id.empty()) {
        log.RecordError(telemetry::obs::kErrHttpBadRequest, "dataset_id required", 400);
        SendError(res, "dataset_id required", 400, telemetry::obs::kErrHttpBadRequest, rid);
        return;
    }
    try {
        auto start = std::chrono::steady_clock::now();
        auto eval = db_client_->GetEvalMetrics(dataset_id, model_run_id, points, max_samples);
        auto end = std::chrono::steady_clock::now();
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            long row_count = eval.value("roc", nlohmann::json::array()).size();
            nlohmann::json resolved;
            resolved["points"] = points;
            resolved["max_samples"] = max_samples;
            eval["debug"] = BuildDebugMeta(duration_ms, row_count, resolved);
        }
        SendJson(res, eval, 200, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::HandleModelErrorDistribution(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    std::string model_run_id = req.matches[1];
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string group_by = GetStrParam(req, "group_by");
    log.AddFields({{"model_run_id", model_run_id}, {"dataset_id", dataset_id}});
    bool debug = GetStrParam(req, "debug") == "true";
    if (dataset_id.empty() || group_by.empty()) {
        log.RecordError(telemetry::obs::kErrHttpBadRequest, "dataset_id and group_by required", 400);
        SendError(res, "dataset_id and group_by required", 400, telemetry::obs::kErrHttpBadRequest, rid);
        return;
    }
    std::unordered_map<std::string, std::string> allowed = {
        {"anomaly_type", "h.anomaly_type"},
        {"region", "h.region"},
        {"project_id", "h.project_id"}
    };
    if (allowed.find(group_by) == allowed.end()) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "Invalid group_by", 400);
        SendError(res, "Invalid group_by", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
        return;
    }
    try {
        auto start = std::chrono::steady_clock::now();
        auto dist = db_client_->GetErrorDistribution(dataset_id, model_run_id, allowed[group_by]);
        auto end = std::chrono::steady_clock::now();
        nlohmann::json resp;
        resp["items"] = dist;
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            nlohmann::json resolved;
            resolved["group_by"] = allowed[group_by];
            resp["debug"] = BuildDebugMeta(duration_ms, static_cast<long>(dist.size()), resolved);
        }
        SendJson(res, resp, 200, rid);
    } catch (const std::invalid_argument& e) {
        log.RecordError(telemetry::obs::kErrHttpInvalidArgument, e.what(), 400);
        SendError(res, e.what(), 400, telemetry::obs::kErrHttpInvalidArgument, rid);
    } catch (const std::exception& e) {
        log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
        SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
    }
}

void ApiServer::SendJson(httplib::Response& res, nlohmann::json j, int status, const std::string& request_id) {
    if (!request_id.empty() && !j.contains("request_id")) {
        j["request_id"] = request_id;
    }
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void ApiServer::SendError(httplib::Response& res, 
                        const std::string& msg, 
                        int status,
                        const std::string& code,
                        const std::string& request_id) {
    telemetry::metrics::MetricsRegistry::Instance().Increment("http_errors_total", {{"status", std::to_string(status)}, {"code", code}});
    nlohmann::json j;
    j["error"]["message"] = msg;
    j["error"]["code"] = code;
    if (!request_id.empty()) {
        j["error"]["request_id"] = request_id;
    }
    SendJson(res, j, status, request_id);
}

int ApiServer::GetIntParam(const httplib::Request& req, const std::string& key, int def) {
    if (!req.has_param(key.c_str())) return def;
    try {
        return std::stoi(req.get_param_value(key.c_str()));
    } catch (...) {
        return def;
    }
}

double ApiServer::GetDoubleParam(const httplib::Request& req, const std::string& key, double def) {
    if (!req.has_param(key.c_str())) return def;
    try {
        return std::stod(req.get_param_value(key.c_str()));
    } catch (...) {
        return def;
    }
}

std::string ApiServer::GetStrParam(const httplib::Request& req, const std::string& key) {
    if (!req.has_param(key.c_str())) return "";
    return req.get_param_value(key.c_str());
}

void ApiServer::ValidateRoutes() {
    // Basic sanity check: ensure registry matches expected count
    // We don't do deep introspection of httplib because it's hard.
    if (kRequiredRoutes.size() != 31) {
        spdlog::warn("Route registry count mismatch! Expected 31, got {}", kRequiredRoutes.size());
    } else {
        spdlog::info("Route registry validated ({} routes)", kRequiredRoutes.size());
    }
}

} // namespace api
} // namespace telemetry
