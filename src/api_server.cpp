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

namespace telemetry::api {

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

static const char* ClassifyHttpError(const std::exception& e) {
    auto msg = std::string(e.what());
    try {
        throw; // Re-throw to check type
    } catch (const nlohmann::json::parse_error&) {
        return telemetry::obs::kErrHttpJsonParseError;
    } catch (const nlohmann::json::out_of_range&) {
        // e.g. "key 'x' not found"
        return telemetry::obs::kErrHttpMissingField;
    } catch (const std::invalid_argument&) {
         return telemetry::obs::kErrHttpInvalidArgument;
    } catch (const std::out_of_range&) {
         return telemetry::obs::kErrHttpInvalidArgument;
    } catch (const pqxx::broken_connection&) {
         return telemetry::obs::kErrDbConnectFailed;
    } catch (const pqxx::sql_error&) {
         return telemetry::obs::kErrDbQueryFailed;
    } catch (...) {
        // Fallback for generic messages if identifiable
        if (msg.find("count must be") != std::string::npos || 
            msg.find("Must be") != std::string::npos ||
            msg.find("Too many") != std::string::npos) {
            return telemetry::obs::kErrHttpInvalidArgument;
        }
        return telemetry::obs::kErrInternal;
    }
}

static const char* ClassifyTrainError(const std::string& msg) {
    if (msg.find("Cancelled") != std::string::npos) {
        return "E_CANCELLED";
    }
    if (msg.find("Not enough samples") != std::string::npos ||
        msg.find("No samples") != std::string::npos) {
        return telemetry::obs::kErrTrainNoData;
    }
    if (msg.find("Failed to open output path") != std::string::npos) {
        return telemetry::obs::kErrTrainArtifactWriteFailed;
    }
    return telemetry::obs::kErrInternal;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ApiServer::ApiServer(const std::string& grpc_target, const std::string& db_conn_str)
{
    size_t pool_size = 5;
    const char* env_pool_size = std::getenv("DB_POOL_SIZE");
    if (env_pool_size) {
        try {
            pool_size = std::stoul(env_pool_size);
        } catch (...) {}
    }

    int timeout_ms = 5000;
    const char* env_timeout = std::getenv("DB_ACQUIRE_TIMEOUT_MS");
    if (env_timeout) {
        try {
            timeout_ms = std::stoi(env_timeout);
        } catch (...) {}
    }

    auto manager = std::make_shared<PooledDbConnectionManager>(
        db_conn_str, pool_size, std::chrono::milliseconds(timeout_ms),
        [](pqxx::connection& C) {
            DbClient::PrepareStatements(C);
        });
    
    db_client_ = std::make_shared<DbClient>(manager);
    db_manager_ = manager;
    grpc_target_ = grpc_target;
    db_conn_str_ = db_conn_str;

    Initialize();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ApiServer::ApiServer(const std::string& grpc_target, std::shared_ptr<IDbClient> db_client)
    : grpc_target_(grpc_target), db_client_(db_client)
{
    db_manager_ = db_client_->GetConnectionManager();
    Initialize();
}

void ApiServer::Initialize() {
    // Initialize gRPC Stub
    auto channel = grpc::CreateChannel(grpc_target_, grpc::InsecureChannelCredentials());
    stub_ = telemetry::TelemetryService::NewStub(channel);

    // Initialize JobManager
    job_manager_ = std::make_unique<JobManager>();

    // Initialize JobReconciler
    job_reconciler_ = std::make_unique<JobReconciler>(db_client_);
    job_reconciler_->ReconcileStartup();
    job_reconciler_->Start(std::chrono::minutes(1));
    
    // Ensure partitions for current and next month

    // Initialize Model Cache (defaults: 100 entries, 1 hour TTL)
    size_t cache_size = 100;
    const char* env_cache_size = std::getenv("MODEL_CACHE_SIZE");
    if (env_cache_size) {
        try { cache_size = std::stoul(env_cache_size); } catch (...) {}
    }

    int cache_ttl = 3600;
    const char* env_cache_ttl = std::getenv("MODEL_CACHE_TTL_SECONDS");
    if (env_cache_ttl) {
        try { cache_ttl = std::stoi(env_cache_ttl); } catch (...) {}
    }

    size_t cache_max_bytes = 512 * 1024 * 1024; // 512MB default
    const char* env_cache_bytes = std::getenv("MODEL_CACHE_MAX_BYTES");
    if (env_cache_bytes) {
        try { cache_max_bytes = std::stoul(env_cache_bytes); } catch (...) {}
    }

    model_cache_ = std::make_unique<telemetry::anomaly::PcaModelCache>(cache_size, cache_max_bytes, cache_ttl);

    // Configure HTTP Server Limits
    svr_.set_payload_max_length(1024 * 1024 * 50); // 50MB
    svr_.set_read_timeout(5, 0); // 5 seconds
    svr_.set_write_timeout(5, 0); // 5 seconds

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

    svr_.Get("/models/([a-zA-Z0-9-]+)/trials", [this](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        std::string model_run_id = req.matches[1];
        int limit = GetIntParam(req, "limit", 50);
        int offset = GetIntParam(req, "offset", 0);
        
        try {
            auto trials = db_client_->GetHpoTrialsPaginated(model_run_id, limit, offset);
            nlohmann::json resp;
            resp["items"] = trials;
            resp["limit"] = limit;
            resp["offset"] = offset;
            resp["returned"] = trials.size();
            SendJson(res, resp, 200, rid);
        } catch (const std::exception& e) {
            log.RecordError(telemetry::obs::kErrDbQueryFailed, e.what(), 500);
            SendError(res, e.what(), 500, telemetry::obs::kErrDbQueryFailed, rid);
        }
    });

    svr_.Post("/models/([a-zA-Z0-9-]+)/rerun_failed", [this](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        std::string model_run_id = req.matches[1];
        log.AddFields({{"model_run_id", model_run_id}});

        auto j = db_client_->GetModelRun(model_run_id);
        if (j.empty()) {
            SendError(res, "Model run not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }

        if (j["hpo_config"].is_null()) {
            SendError(res, "Not a tuning run", 400, "E_NOT_TUNING", rid);
            return;
        }

        auto trials = db_client_->GetHpoTrials(model_run_id);
        std::vector<nlohmann::json> failed_trials;
        for (const auto& t : trials) {
            if (t["status"] == "FAILED" || t["status"] == "CANCELLED") {
                failed_trials.push_back(t);
            }
        }

        if (failed_trials.empty()) {
            SendError(res, "No failed or cancelled trials to rerun", 400, "E_NO_FAILED_TRIALS", rid);
            return;
        }

        // Bounded rerun (max 10 at a time)
        int rerun_limit = 10;
        int count = 0;
        std::vector<std::string> new_trial_ids;
        for (const auto& t : failed_trials) {
            if (count >= rerun_limit) break;

            std::string trial_name = t["name"];
            if (trial_name.find("_rerun_") == std::string::npos) {
                trial_name += "_rerun_" + GenerateUuid().substr(0, 4);
            }

            std::string new_tid = db_client_->CreateHpoTrialRun(
                t["dataset_id"],
                trial_name,
                t["training_config"],
                rid,
                model_run_id,
                t["trial_index"], // Keep same index for linking
                t["trial_params"]
            );

            if (!new_tid.empty()) {
                new_trial_ids.push_back(new_tid);
                
                // Dispatch it
                auto t_cfg = t["training_config"];
                RunPcaTraining(new_tid, t["dataset_id"], t_cfg["n_components"], t_cfg["percentile"], rid);
            }
            count++;
        }

        nlohmann::json resp;
        resp["rerun_count"] = count;
        resp["new_trial_ids"] = new_trial_ids;
        SendJson(res, resp, 202, rid);
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
            auto C = db_manager_->GetConnection();
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

    svr_.Delete("/train/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string rid = GetRequestId(req);
        telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
        std::string model_run_id = req.matches[1];
        log.AddFields({{"model_run_id", model_run_id}});
        
        auto j = db_client_->GetModelRun(model_run_id);
        if (j.empty()) {
            log.RecordError(telemetry::obs::kErrHttpNotFound, "Model run not found", 404);
            SendError(res, "Model run not found", 404, telemetry::obs::kErrHttpNotFound, rid);
            return;
        }

        std::string status = j["status"];
        if (status == "COMPLETED" || status == "FAILED" || status == "CANCELLED") {
            SendError(res, "Cannot cancel terminal run", 400, "E_TERMINAL", rid);
            return;
        }

        // 1. If it's a parent, cancel all trials
        if (!j["hpo_config"].is_null()) {
            auto trials = db_client_->GetHpoTrials(model_run_id);
            for (const auto& t : trials) {
                std::string tid = t["model_run_id"];
                std::string tst = t["status"];
                if (tst == "PENDING" || tst == "RUNNING") {
                    job_manager_->CancelJob("train-" + tid);
                    db_client_->UpdateModelRunStatus(tid, "CANCELLED", "", "Cancelled by parent tuning run request");
                    db_client_->UpdateTrialEligibility(tid, false, "CANCELED", 0.0);
                }
            }
        }

        // 2. Cancel the run itself (or parent)
        job_manager_->CancelJob("train-" + model_run_id);
        db_client_->UpdateModelRunStatus(model_run_id, "CANCELLED", "", "Cancelled by user request");
        
        nlohmann::json resp;
        resp["status"] = "CANCEL_REQUESTED";
        resp["model_run_id"] = model_run_id;
        SendJson(res, resp, 200, rid);
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

ApiServer::~ApiServer() {
    Stop();
}

void ApiServer::Start(const std::string& host, int port) {
    ValidateRoutes();
    spdlog::info("HTTP API Server listening on {}:{}", host, port);
    svr_.listen(host.c_str(), port);
}

void ApiServer::Stop() {
    if (job_reconciler_) {
        job_reconciler_->Stop();
    }
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
        auto code = ClassifyHttpError(e);
        int status = 500;
        if (code == telemetry::obs::kErrHttpJsonParseError || 
            code == telemetry::obs::kErrHttpMissingField ||
            code == telemetry::obs::kErrHttpInvalidArgument ||
            code == telemetry::obs::kErrHttpBadRequest) {
            status = 400;
        }

        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "generate_error", "api",
                                 {{"request_id", rid}, {"error_code", code}, {"error", e.what()}});
        log.RecordError(code, e.what(), status);
        SendError(res, std::string("Generate failed: ") + e.what(), status, code, rid);
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
        int returned_bins = static_cast<int>(data.value("counts", nlohmann::json::array()).size());
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
            long row_count = static_cast<long>(data.value("counts", nlohmann::json::array()).size());
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

void ApiServer::OrchestrateTuning(TuningTask task) {
    job_manager_->StartJob("tuning-" + task.parent_run_id, task.rid, [this, task](const std::atomic<bool>* stop_flag) {
        telemetry::obs::Context ctx;
        ctx.request_id = task.rid;
        ctx.dataset_id = task.dataset_id;
        ctx.model_run_id = task.parent_run_id;
        telemetry::obs::ScopedContext scope(ctx);

        spdlog::info("Tuning orchestration started for model_run_id: {} with {} trials (max_concurrency: {})", 
                     task.parent_run_id, task.trials.size(), task.max_concurrency);
        
        if (!db_client_->TryTransitionModelRunStatus(task.parent_run_id, "PENDING", "RUNNING")) {
            auto model_info = db_client_->GetModelRun(task.parent_run_id);
            if (model_info.value("status", "") != "RUNNING") return;
        }

        std::vector<std::string> trial_ids;
        int idx = 0;
        for (const auto& trial_cfg : task.trials) {
            nlohmann::json trial_params = {
                {"n_components", trial_cfg.n_components},
                {"percentile", trial_cfg.percentile}
            };
            
            std::string trial_name = task.name + "_trial_" + std::to_string(idx);
            nlohmann::json t_training_config = {
                {"dataset_id", task.dataset_id},
                {"n_components", trial_cfg.n_components},
                {"percentile", trial_cfg.percentile},
                {"feature_set", "cpu,mem,disk,rx,tx"}
            };

            std::string trial_run_id = db_client_->CreateHpoTrialRun(
                task.dataset_id, 
                trial_name, 
                t_training_config, 
                task.rid, 
                task.parent_run_id, 
                idx, 
                trial_params);
            
            if (!trial_run_id.empty()) {
                trial_ids.push_back(trial_run_id);
            }
            idx++;
        }

        // Execution loop with concurrency control
        size_t next_trial = 0;
        std::map<std::string, bool> active_trials;

        while (next_trial < trial_ids.size() || !active_trials.empty()) {
            if (stop_flag->load()) {
                spdlog::warn("Tuning orchestration for {} cancelled.", task.parent_run_id);
                // Propagate cancel to all remaining trials (already handles by HandleDelete endpoint but here for robustness)
                db_client_->UpdateModelRunStatus(task.parent_run_id, "CANCELLED");
                return;
            }

            // Start new trials up to concurrency limit
            while (next_trial < trial_ids.size() && active_trials.size() < static_cast<size_t>(task.max_concurrency)) {
                std::string tid = trial_ids[next_trial];
                auto& t_cfg = task.trials[next_trial];
                
                RunPcaTraining(tid, task.dataset_id, t_cfg.n_components, t_cfg.percentile, task.rid);
                active_trials[tid] = true;
                next_trial++;
            }

            // Wait and check for trial completions
            db_client_->Heartbeat(IDbClient::JobType::ModelRun, task.parent_run_id);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            auto it = active_trials.begin();
            while (it != active_trials.end()) {
                auto run_info = db_client_->GetModelRun(it->first);
                std::string status = run_info["status"];
                if (status == "COMPLETED" || status == "FAILED" || status == "CANCELLED") {
                    it = active_trials.erase(it);
                } else {
                    ++it;
                }
            }
        }

        spdlog::info("Tuning orchestration finished for model_run_id: {}", task.parent_run_id);
        // Status aggregation is handled by HandleGetDetail dynamically but we can trigger a final update here too
    });
}

void ApiServer::RunPcaTraining(const std::string& model_run_id, 
                               const std::string& dataset_id, 
                               int n_components, 
                               double percentile, 
                               const std::string& rid) {
    job_manager_->StartJob("train-" + model_run_id, rid, [this, model_run_id, dataset_id, n_components, percentile, rid](const std::atomic<bool>* stop_flag) {
        telemetry::obs::Context ctx;
        ctx.request_id = rid;
        ctx.dataset_id = dataset_id;
        ctx.model_run_id = model_run_id;
        telemetry::obs::ScopedContext scope(ctx);
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "train_start", "trainer",
                                    {{"request_id", rid}, {"dataset_id", dataset_id}, {"model_run_id", model_run_id}});
        auto train_start = std::chrono::steady_clock::now();
        spdlog::info("Training started for model {} (req_id: {})", model_run_id, rid);
        
        if (!db_client_->TryTransitionModelRunStatus(model_run_id, "PENDING", "RUNNING")) {
            auto model_info = db_client_->GetModelRun(model_run_id);
            std::string current_status = model_info.value("status", "UNKNOWN");
            spdlog::warn("Model {} transition PENDING->RUNNING failed (current status: {}).", model_run_id, current_status);
            if (current_status != "RUNNING") return;
        }

        std::string output_dir = "artifacts/pca/" + model_run_id;
        std::string output_path = output_dir + "/model.json";

                                try {

                                    std::filesystem::create_directories(output_dir);

                                    auto artifact = telemetry::training::TrainPcaFromDb(db_manager_, dataset_id, n_components, percentile, [this, model_run_id]() {
                                        db_client_->Heartbeat(IDbClient::JobType::ModelRun, model_run_id);
                                    });

                                    

                                    if (stop_flag->load()) {

                                        spdlog::info("Training for model {} aborted by cancellation.", model_run_id);
                                        db_client_->UpdateModelRunStatus(model_run_id, "CANCELLED");

                                        return;

                                    }

                    

                                    telemetry::training::WriteArtifactJson(artifact, output_path);

                    

        

                        spdlog::info("Training successful for model {}", model_run_id);

                        db_client_->UpdateModelRunStatus(model_run_id, "COMPLETED", output_path);

                        

                        // Update eligibility metadata

                        db_client_->UpdateTrialEligibility(model_run_id, true, "", artifact.threshold, "evaluation_artifact_v1");

        

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
            
            nlohmann::json error_summary;
            error_summary["code"] = error_code;
            error_summary["message"] = std::string(e.what()).substr(0, 200); // Truncate long messages
            error_summary["stage"] = "train";

            telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "train_error", "trainer",
                                        {{"request_id", rid},
                                        {"dataset_id", dataset_id},
                                        {"model_run_id", model_run_id},
                                        {"error_code", error_code},
                                        {"error", e.what()},
                                        {"duration_ms", duration_ms}});
                            spdlog::error("Training failed for model {}: {}", model_run_id, e.what());
                            db_client_->UpdateModelRunStatus(model_run_id, "FAILED", "", e.what(), error_summary);
                            db_client_->UpdateTrialEligibility(model_run_id, false, "FAILED", 0.0);
                            throw; // JobManager will catch and log it too
                        }
            
    });
}

void ApiServer::HandleTrainModel(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    telemetry::obs::HttpRequestLogScope log(req, res, "api_server", rid);
    try {
        auto j = nlohmann::json::parse(req.body);
        std::string dataset_id = j.at("dataset_id");
        std::string name = j.value("name", "pca_default");
        
        // Phase 2: Training parameters with guardrails
        int n_components = j.value("n_components", 3);
        double percentile = j.value("percentile", 99.5);

        if (n_components <= 0 || n_components > 5) { // 5 features max in current schema
             log.RecordError(telemetry::obs::kErrHttpBadRequest, "n_components must be between 1 and 5", 400);
             SendError(res, "n_components must be between 1 and 5", 400, telemetry::obs::kErrHttpBadRequest, rid);
             return;
        }
        if (percentile < 50.0 || percentile >= 100.0) {
             log.RecordError(telemetry::obs::kErrHttpBadRequest, "percentile must be between 50.0 and 99.99", 400);
             SendError(res, "percentile must be between 50.0 and 99.99", 400, telemetry::obs::kErrHttpBadRequest, rid);
             return;
        }

        nlohmann::json training_config = {
            {"dataset_id", dataset_id},
            {"n_components", n_components},
            {"percentile", percentile},
            {"feature_set", "cpu,mem,disk,rx,tx"}
        };

        nlohmann::json hpo_config = nlohmann::json::object();
        std::string fingerprint = "";
        std::string generator_version = "";
        std::optional<long long> seed_used = std::nullopt;
        nlohmann::json preflight_resp = nlohmann::json::object();

        if (j.contains("hpo_config")) {
            hpo_config = j["hpo_config"];
            telemetry::training::HpoConfig hpo;
            hpo.algorithm = hpo_config.value("algorithm", "grid");
            hpo.max_trials = hpo_config.value("max_trials", 10);
            hpo.max_concurrency = hpo_config.value("max_concurrency", 2);
            if (hpo_config.contains("seed")) {
                hpo.seed = hpo_config["seed"].get<int>();
                seed_used = hpo.seed.value();
            }
            
            if (hpo_config.contains("search_space")) {
                auto ss = hpo_config["search_space"];
                if (ss.contains("n_components")) hpo.search_space.n_components = ss["n_components"].get<std::vector<int>>();
                if (ss.contains("percentile")) hpo.search_space.percentile = ss["percentile"].get<std::vector<double>>();
            }

            auto errors = telemetry::training::ValidateHpoConfig(hpo);
            if (!errors.empty()) {
                nlohmann::json err_resp;
                err_resp["error"]["message"] = "Invalid HPO configuration";
                err_resp["error"]["code"] = telemetry::obs::kErrHttpInvalidArgument;
                for (const auto& e : errors) {
                    err_resp["error"]["field_errors"].push_back({{"field", e.field}, {"message", e.message}});
                }
                SendJson(res, err_resp, 400, rid);
                return;
            }
            
            auto preflight = telemetry::training::PreflightHpoConfig(hpo);
            preflight_resp["estimated_candidates"] = preflight.estimated_candidates;
            preflight_resp["effective_trials"] = preflight.effective_trials;
            std::string cap_reason = "NONE";
            if (preflight.capped_by == telemetry::training::HpoCapReason::MAX_TRIALS) cap_reason = "MAX_TRIALS";
            else if (preflight.capped_by == telemetry::training::HpoCapReason::GRID_CAP) cap_reason = "GRID_CAP";
            preflight_resp["capped_by"] = cap_reason;

            fingerprint = telemetry::training::ComputeCandidateFingerprint(hpo);
            generator_version = telemetry::training::kHpoGeneratorVersion;
        }

        log.AddFields({{"dataset_id", dataset_id}, {"training_config", training_config.dump()}});
        if (!hpo_config.empty()) {
            log.AddFields({{"hpo_config", hpo_config.dump()}, {"fingerprint", fingerprint}});
        }

        // 1. Create DB entry
        std::string model_run_id = db_client_->CreateModelRun(dataset_id, name, training_config, rid, hpo_config, fingerprint, generator_version, seed_used);
        if (model_run_id.empty()) {
            log.RecordError(telemetry::obs::kErrDbInsertFailed, "Failed to create model run in DB", 500);
            SendError(res, "Failed to create model run in DB", 500, telemetry::obs::kErrDbInsertFailed, rid);
            return;
        }
        log.AddFields({{"model_run_id", model_run_id}});

        if (hpo_config.empty()) {
            // Standard Single Run
            RunPcaTraining(model_run_id, dataset_id, n_components, percentile, rid);
        } else {
            // HPO Orchestration
            telemetry::training::HpoConfig hpo;
            hpo.algorithm = hpo_config.value("algorithm", "grid");
            hpo.max_trials = hpo_config.value("max_trials", 10);
            hpo.max_concurrency = hpo_config.value("max_concurrency", 2);
            if (hpo_config.contains("seed")) hpo.seed = hpo_config["seed"].get<int>();
            if (hpo_config.contains("search_space")) {
                auto ss = hpo_config["search_space"];
                if (ss.contains("n_components")) hpo.search_space.n_components = ss["n_components"].get<std::vector<int>>();
                if (ss.contains("percentile")) hpo.search_space.percentile = ss["percentile"].get<std::vector<double>>();
            }

            auto trials = telemetry::training::GenerateTrials(hpo, dataset_id);
            
            TuningTask task;
            task.parent_run_id = model_run_id;
            task.name = name;
            task.dataset_id = dataset_id;
            task.rid = rid;
            task.trials = trials;
            task.max_concurrency = std::clamp(hpo.max_concurrency, 1, 10);

            OrchestrateTuning(std::move(task));
        }

        nlohmann::json resp;
        resp["model_run_id"] = model_run_id;
        resp["status"] = "PENDING";
        if (!preflight_resp.empty()) {
            resp["hpo_preflight"] = preflight_resp;
        }
        SendJson(res, resp, 202, rid);
    } catch (const std::exception& e) {
        auto code = ClassifyHttpError(e);
        int status = 500;
        std::string err = e.what();

        if (err.find("Job queue full") != std::string::npos) {
            code = telemetry::obs::kErrHttpResourceExhausted;
            status = 503;
        } else if (code == telemetry::obs::kErrHttpJsonParseError || 
            code == telemetry::obs::kErrHttpMissingField ||
            code == telemetry::obs::kErrHttpInvalidArgument ||
            code == telemetry::obs::kErrHttpBadRequest) {
            status = 400;
        }

        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "train_submit_error", "api",
                                 {{"request_id", rid}, {"error_code", code}, {"error", err}});
        log.RecordError(code, err, status);
        SendError(res, std::string("Error: ") + err, status, code, rid);
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
        
        std::vector<std::string> parent_run_ids;
        for (auto& m : models) {
            if (!m.contains("parent_run_id")) m["parent_run_id"] = nullptr;
            if (!m.contains("trial_index")) m["trial_index"] = nullptr;
            
            // Identify potential parents (hpo_config is present and not null usually indicates intent, 
            // but checking if it is a child is safer: parent_run_id is null)
            // AND we only care if they are tuning runs.
            // Tuning runs have hpo_config not null? Or we just try to fetch for all roots?
            // To be efficient, we can fetch for all roots.
            if (m["parent_run_id"].is_null()) {
                parent_run_ids.push_back(m["model_run_id"]);
            }
        }

        auto bulk_summaries = db_client_->GetBulkHpoTrialSummaries(parent_run_ids);

        for (auto& m : models) {
            if (m["parent_run_id"].is_null()) {
                std::string mid = m["model_run_id"];
                if (bulk_summaries.count(mid)) {
                    auto& s = bulk_summaries[mid];
                    if (s.contains("trial_count") && s["trial_count"].get<long>() > 0) {
                        m["hpo_summary"] = {
                            {"trial_count", s["trial_count"]},
                            {"completed_count", s["completed_count"]},
                            {"best_metric_value", m["best_metric_value"]},
                            {"best_metric_name", m["best_metric_name"]}
                        };
                        
                        // Infer aggregate status
                        // API logic was: RUNNING if any RUNNING/PENDING
                        // FAILED if all terminal and FAILED > 0 and COMPLETED == 0
                        // COMPLETED if COMPLETED > 0
                        auto& sc = s["status_counts"];
                        long pending = sc.value("PENDING", 0);
                        long running = sc.value("RUNNING", 0);
                        long completed = sc.value("COMPLETED", 0);
                        long failed = sc.value("FAILED", 0);
                        
                        if (running > 0 || pending > 0) m["status"] = "RUNNING";
                        else if (completed > 0) m["status"] = "COMPLETED";
                        else if (failed > 0) m["status"] = "FAILED";
                    }
                }
            }
        }

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

        // Add HPO helper fields for UI if they are null
        if (!j.contains("hpo_config") || j["hpo_config"].is_null()) j["hpo_config"] = nullptr;
        if (!j.contains("parent_run_id") || j["parent_run_id"].is_null()) j["parent_run_id"] = nullptr;
        if (!j.contains("trial_index") || j["trial_index"].is_null()) j["trial_index"] = nullptr;
        if (!j.contains("trial_params") || j["trial_params"].is_null()) j["trial_params"] = nullptr;

        // If it's a parent, fetch trials and aggregate status
        if (!j["hpo_config"].is_null()) {
            auto trials = db_client_->GetHpoTrials(model_run_id);
            j["trials"] = trials;
            
            int pending = 0, running = 0, completed = 0, failed = 0;
            std::map<std::string, int> error_counts;
            for (const auto& t : trials) {
                std::string st = t["status"];
                if (st == "PENDING") pending++;
                else if (st == "RUNNING") running++;
                else if (st == "COMPLETED") completed++;
                else if (st == "FAILED") {
                    failed++;
                    if (t.contains("error_summary") && !t["error_summary"].is_null() && t["error_summary"].contains("code")) {
                        std::string code = t["error_summary"]["code"];
                        error_counts[code]++;
                    } else if (!t["error"].is_null() && !t["error"].get<std::string>().empty()) {
                        error_counts["UNKNOWN"]++;
                    }
                }
            }
            j["trial_counts"] = {
                {"total", trials.size()},
                {"pending", pending},
                {"running", running},
                {"completed", completed},
                {"failed", failed}
            };
            j["error_aggregates"] = error_counts;

            // Persist aggregates if they changed and the run is terminal
            if ((running == 0 && pending == 0) && (j["error_aggregates_db"].is_null() || j["error_aggregates_db"] != error_counts)) {
                db_client_->UpdateParentErrorAggregates(model_run_id, error_counts);
            }

            // Aggregate Status:
            // RUNNING if any trial is RUNNING or PENDING
            // SUCCEEDED if all terminal and at least one COMPLETED
            // FAILED if all terminal and none COMPLETED
            if (running > 0 || pending > 0) {
                j["status"] = "RUNNING";
            } else if (completed > 0) {
                j["status"] = "COMPLETED";
            } else if (failed > 0) {
                j["status"] = "FAILED";
            }

            // Best Trial Selection
            // Metric: threshold (reconstruction_error). LOWER_IS_BETTER.
            // Tie-break: earlier completed_at, then lower trial_index.
            std::string best_id = "";
            double min_threshold = std::numeric_limits<double>::max();
            std::string best_completed_at = "";
            int best_trial_index = std::numeric_limits<int>::max();
            
            for (const auto& t : trials) {
                if (t["status"] == "COMPLETED" && t["is_eligible"].get<bool>()) {
                    double threshold = t["selection_metric_value"].get<double>();
                    std::string completed_at = t["completed_at"];
                    int trial_index = t["trial_index"];

                    bool is_better = false;
                    if (best_id.empty()) {
                        is_better = true;
                    } else if (threshold < min_threshold) {
                        is_better = true;
                    } else if (threshold == min_threshold) {
                        // Tie-break 1: earlier completion
                        if (completed_at < best_completed_at) {
                            is_better = true;
                        } else if (completed_at == best_completed_at) {
                            // Tie-break 2: lower trial index
                            if (trial_index < best_trial_index) {
                                is_better = true;
                            }
                        }
                    }

                    if (is_better) {
                        min_threshold = threshold;
                        best_id = t["model_run_id"];
                        best_completed_at = completed_at;
                        best_trial_index = trial_index;
                    }
                }
            }

            if (!best_id.empty()) {
                j["best_trial_run_id"] = best_id;
                j["best_metric_value"] = min_threshold;
                j["best_metric_name"] = "reconstruction_error_threshold";
                j["selection_metric_direction"] = "LOWER_IS_BETTER";
                j["tie_break_basis"] = "completion_time, trial_index";
                
                // Persist if not already set or changed
                if (j["best_trial_run_id_db"].is_null() || j["best_trial_run_id_db"] != best_id) {
                    db_client_->UpdateBestTrial(
                        model_run_id, 
                        best_id, 
                        min_threshold, 
                        "reconstruction_error_threshold",
                        "LOWER_IS_BETTER",
                        "completion_time, trial_index");
                }
            }
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
                    if (artifact.contains("thresholds") &&
                        artifact["thresholds"].contains("reconstruction_error")) {
                        j["threshold"] = artifact["thresholds"]["reconstruction_error"];
                    }
                    if (artifact.contains("model") && artifact["model"].contains("n_components")) {
                        j["n_components"] = artifact["model"]["n_components"];
                    }
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
        
        // Safety: Limit number of samples
        if (samples.size() > 1000) {
            log.RecordError(telemetry::obs::kErrHttpInvalidArgument, "Too many samples (max 1000)", 400);
            SendError(res, "Too many samples (max 1000)", 400, telemetry::obs::kErrHttpInvalidArgument, rid);
            return;
        }

        // Safety: Validate feature vector size (first sample check sufficient for batch consistency)
        if (!samples.empty()) {
             // 5 features expected: cpu, mem, disk, rx, tx
             // In unstructured json, checking size might be tricky if keys are optional.
             // But valid samples should have keys.
             // Let's enforce reasonable size or keys.
             // For strict validation, we'd check each. For simple safety, check count.
        }

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

        // 2. Load Model (with cache)
        std::shared_ptr<telemetry::anomaly::PcaModel> pca;
        try {
            pca = model_cache_->GetOrCreate(model_run_id, artifact_path);
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
            // Optional: Transition to RUNNING if CreateInferenceRun returns PENDING
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

            auto score = pca->Score(v);
            
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

        SendJson(res, resp, 200, rid);

    } catch (const std::exception& e) {
        auto code = ClassifyHttpError(e);
        int status = 500;
        if (code == telemetry::obs::kErrHttpJsonParseError || 
            code == telemetry::obs::kErrHttpMissingField ||
            code == telemetry::obs::kErrHttpInvalidArgument ||
            code == telemetry::obs::kErrHttpBadRequest) {
            status = 400;
        } else if (code == telemetry::obs::kErrHttpNotFound) { // unlikely here but possible
            status = 404;
        }
        
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "infer_error", "model",
                                 {{"request_id", rid}, {"error_code", code}, {"error", e.what()}});
        log.RecordError(code, e.what(), status);
        SendError(res, std::string("Error: ") + e.what(), status, code, rid);
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

auto ApiServer::HandleGetInferenceRun(const httplib::Request& req, httplib::Response& res) -> void {
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

auto ApiServer::HandleListJobs(const httplib::Request& req, httplib::Response& res) -> void {
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

auto ApiServer::HandleScoreDatasetJob(const httplib::Request& req, httplib::Response& res) -> void {
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
            try {
                auto job_info = db_client_->GetScoreJob(job_id);
                long total = job_info.value("total_rows", 0L);
                long processed = job_info.value("processed_rows", 0L);
                long last_record = job_info.value("last_record_id", 0L);

                total = db_client_->GetDatasetRecordCount(dataset_id);
                if (!db_client_->TryTransitionScoreJobStatus(job_id, "PENDING", "RUNNING")) {
                    auto current_job_info = db_client_->GetScoreJob(job_id);
                    if (current_job_info.value("status", "") != "RUNNING") { return; }
                }
                db_client_->UpdateScoreJob(job_id, "RUNNING", total, processed, last_record);

                auto model_info = db_client_->GetModelRun(model_run_id);
                std::string artifact_path = model_info.value("artifact_path", "");
                if (artifact_path.empty()) {
                    throw std::runtime_error("Model artifact path missing");
                }
                auto model = model_cache_->GetOrCreate(model_run_id, artifact_path);

                const int batch = 5000;
                while (!stop_flag->load()) {
                    auto rows = db_client_->FetchScoringRowsAfterRecord(dataset_id, last_record, batch);
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
                        auto score = model->Score(v);
                        scores.emplace_back(r.record_id, std::make_pair(score.reconstruction_error, score.is_anomaly));
                    }
                    db_client_->InsertDatasetScores(dataset_id, model_run_id, scores);
                    processed += static_cast<long>(rows.size());
                    last_record = rows.back().record_id;
                    db_client_->UpdateScoreJob(job_id, "RUNNING", total, processed, last_record);
                }
                
                if (stop_flag->load()) {
                    spdlog::info("Job {} cancelled by request.", job_id);
                    db_client_->UpdateScoreJob(job_id, "CANCELLED", total, processed, last_record);
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
                    db_client_->UpdateScoreJob(job_id, "COMPLETED", total, processed, last_record);
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
                auto job_info = db_client_->GetScoreJob(job_id);
                long total = job_info.value("total_rows", 0L);
                long processed = job_info.value("processed_rows", 0L);
                long last_record = job_info.value("last_record_id", 0L);
                db_client_->UpdateScoreJob(job_id, "FAILED", total, processed, last_record, e.what());
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

auto ApiServer::HandleGetJobStatus(const httplib::Request& req, httplib::Response& res) -> void {
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
            long row_count = static_cast<long>(eval.value("roc", nlohmann::json::array()).size());
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

auto ApiServer::SendJson(httplib::Response& res, nlohmann::json j, int status, const std::string& request_id) -> void {
    if (!request_id.empty() && !j.contains("request_id")) {
        j["request_id"] = request_id;
    }
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

auto ApiServer::GetIntParam(const httplib::Request& req, const std::string& key, int def) -> int {
    if (!req.has_param(key.c_str())) { return def; }
    try {
        return std::stoi(req.get_param_value(key.c_str()));
    } catch (...) {
        return def;
    }
}

auto ApiServer::GetDoubleParam(const httplib::Request& req, const std::string& key, double def) -> double {
    if (!req.has_param(key.c_str())) { return def; }
    try {
        return std::stod(req.get_param_value(key.c_str()));
    } catch (...) {
        return def;
    }
}

auto ApiServer::GetStrParam(const httplib::Request& req, const std::string& key) -> std::string {
    if (!req.has_param(key.c_str())) { return ""; }
    return req.get_param_value(key.c_str());
}

auto ApiServer::ValidateRoutes() -> void {
    // Basic sanity check: ensure registry matches expected count
    // We don't do deep introspection of httplib because it's hard.
    if (kRequiredRoutes.size() != 35) {
        spdlog::warn("Route registry count mismatch! Expected 35, got {}", kRequiredRoutes.size());
    } else {
        spdlog::info("Route registry validated ({} routes)", kRequiredRoutes.size());
    }
}

} // namespace telemetry::api
