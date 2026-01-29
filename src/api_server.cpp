#include "detectors/pca_model.h"
#include "api_server.h"
#include "api_debug.h"
#include "training/pca_trainer.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <pqxx/pqxx>
#include "detectors/detector_a.h"
#include "preprocessing.h"
#include "detector_config.h"

#include <uuid/uuid.h>

namespace telemetry {
namespace api {

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

ApiServer::ApiServer(const std::string& grpc_target, const std::string& db_conn_str)
    : grpc_target_(grpc_target), db_conn_str_(db_conn_str)
{
    // Initialize gRPC Stub
    auto channel = grpc::CreateChannel(grpc_target, grpc::InsecureChannelCredentials());
    stub_ = telemetry::TelemetryService::NewStub(channel);

    // Initialize DB Client
    db_client_ = std::make_unique<DbClient>(db_conn_str);

    // Initialize Job Manager
    job_manager_ = std::make_unique<JobManager>();

    // Setup Routes
    svr_.Post("/datasets", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGenerateData(req, res);
    });

    svr_.Get("/datasets", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListDatasets(req, res);
    });

    svr_.Get("/datasets/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDataset(req, res);
    });

    svr_.Get("/datasets/:id/summary", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetSummary(req, res);
    });

    svr_.Get("/datasets/:id/topk", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetTopK(req, res);
    });

    svr_.Get("/datasets/:id/timeseries", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetTimeSeries(req, res);
    });

    svr_.Get("/datasets/:id/histogram", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDatasetHistogram(req, res);
    });

    svr_.Post("/train", [this](const httplib::Request& req, httplib::Response& res) {
        HandleTrainModel(req, res);
    });

    svr_.Get("/train/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetTrainStatus(req, res);
    });

    svr_.Get("/models", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListModels(req, res);
    });

    svr_.Get("/models/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetModelDetail(req, res);
    });

    svr_.Post("/inference", [this](const httplib::Request& req, httplib::Response& res) {
        HandleInference(req, res);
    });

    svr_.Get("/inference_runs", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListInferenceRuns(req, res);
    });

    svr_.Get("/inference_runs/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetInferenceRun(req, res);
    });

    svr_.Post("/jobs/score_dataset", [this](const httplib::Request& req, httplib::Response& res) {
        HandleScoreDatasetJob(req, res);
    });

    svr_.Get("/jobs", [this](const httplib::Request& req, httplib::Response& res) {
        HandleListJobs(req, res);
    });

    svr_.Get("/jobs/:id/progress", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetJobProgress(req, res);
    });

    svr_.Get("/jobs/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetJobStatus(req, res);
    });

    svr_.Get("/models/:id/eval", [this](const httplib::Request& req, httplib::Response& res) {
        HandleModelEval(req, res);
    });

    svr_.Get("/models/:id/error_distribution", [this](const httplib::Request& req, httplib::Response& res) {
        HandleModelErrorDistribution(req, res);
    });

    svr_.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });

    svr_.Get("/readyz", [this](const httplib::Request&, httplib::Response& res) {
        try {
            pqxx::connection C(db_conn_str_);
            res.status = 200;
            res.set_content("{\"status\":\"READY\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 503;
            res.set_content("{\"status\":\"UNREADY\", \"reason\":\"DB_CONNECTION_FAILED\"}", "application/json");
        }
    });

    // Serve Static Web UI
    svr_.set_mount_point("/", "./www");

    // CORS Support
    svr_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
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
    spdlog::info("HTTP API Server listening on {}:{}", host, port);
    svr_.listen(host.c_str(), port);
}

void ApiServer::Stop() {
    svr_.stop();
}

void ApiServer::HandleGenerateData(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    try {
        auto j = nlohmann::json::parse(req.body);
        int host_count = j.value("host_count", 5);
        std::string run_id = j.value("run_id", ""); // Optional, will be generated if empty

        grpc::ClientContext context;
        telemetry::GenerateRequest g_req;
        g_req.set_host_count(host_count);
        g_req.set_tier("USER_UI");
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
            resp["request_id"] = rid;
            SendJson(res, resp, 202);
        } else {
            SendError(res, "gRPC Error: " + status.error_message(), 500, "GRPC_ERROR", rid);
        }
    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what(), 400, "BAD_REQUEST", rid);
    }
}

void ApiServer::HandleListDatasets(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
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
        resp["request_id"] = rid;
        SendJson(res, resp);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleGetDataset(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string run_id = req.matches[1];
    try {
        auto detail = db_client_->GetDatasetDetail(run_id);
        if (!detail.empty()) {
            detail["request_id"] = rid;
            SendJson(res, detail);
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
        resp["request_id"] = rid;
        SendJson(res, resp);
    } else {
        SendError(res, "gRPC Error: " + status.error_message(), 404, "NOT_FOUND", rid);
    }
}

void ApiServer::HandleDatasetSummary(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string run_id = req.matches[1];
    int topk = GetIntParam(req, "topk", 5);
    bool debug = GetStrParam(req, "debug") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto summary = db_client_->GetDatasetSummary(run_id, topk);
        auto end = std::chrono::steady_clock::now();
        if (summary.empty()) {
            SendError(res, "Dataset not found", 404, "NOT_FOUND", rid);
            return;
        }
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            long row_count = summary.value("row_count", 0L);
            summary["debug"] = BuildDebugMeta(duration_ms, row_count);
        }
        summary["request_id"] = rid;
        SendJson(res, summary);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleDatasetTopK(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string run_id = req.matches[1];
    std::string column = GetStrParam(req, "column");
    int k = GetIntParam(req, "k", 10);
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
        SendError(res, "Invalid column", 400, "INVALID_ARGUMENT", rid);
        return;
    }
    bool debug = GetStrParam(req, "debug") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto data = db_client_->GetTopK(run_id, allowed[column], k, is_anomaly, anomaly_type, start_time, end_time);
        auto end = std::chrono::steady_clock::now();
        nlohmann::json resp;
        resp["items"] = data;
        resp["request_id"] = rid;
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            nlohmann::json resolved;
            resolved["column"] = allowed[column];
            resp["debug"] = BuildDebugMeta(duration_ms, static_cast<long>(data.size()), resolved);
        }
        SendJson(res, resp);
    } catch (const std::invalid_argument& e) {
        SendError(res, e.what(), 400, "INVALID_ARGUMENT", rid);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleDatasetTimeSeries(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string run_id = req.matches[1];
    std::string metrics_param = GetStrParam(req, "metrics");
    // ... (rest of parsing)
    std::string aggs_param = GetStrParam(req, "aggs");
    std::string bucket = GetStrParam(req, "bucket");
    std::string is_anomaly = GetStrParam(req, "is_anomaly");
    std::string anomaly_type = GetStrParam(req, "anomaly_type");
    std::string start_time = GetStrParam(req, "start_time");
    std::string end_time = GetStrParam(req, "end_time");

    std::vector<std::string> metrics;
    std::stringstream ms(metrics_param);
    std::string token;
    while (std::getline(ms, token, ',')) {
        if (!token.empty()) metrics.push_back(token);
    }
    if (metrics.empty()) {
        SendError(res, "metrics required", 400, "INVALID_ARGUMENT", rid);
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
    else if (bucket == "1h" || bucket.empty()) bucket_seconds = 3600;
    else if (bucket == "1d") bucket_seconds = 86400;

    bool debug = GetStrParam(req, "debug") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto data = db_client_->GetTimeSeries(run_id, metrics, aggs, bucket_seconds, is_anomaly, anomaly_type, start_time, end_time);
        auto end = std::chrono::steady_clock::now();
        nlohmann::json resp;
        resp["items"] = data;
        resp["bucket_seconds"] = bucket_seconds;
        resp["request_id"] = rid;
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            nlohmann::json resolved;
            resolved["metrics"] = metrics;
            resolved["aggs"] = aggs;
            resolved["bucket_seconds"] = bucket_seconds;
            resp["debug"] = BuildDebugMeta(duration_ms, static_cast<long>(data.size()), resolved);
        }
        SendJson(res, resp);
    } catch (const std::invalid_argument& e) {
        SendError(res, e.what(), 400, "INVALID_ARGUMENT", rid);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleDatasetHistogram(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string run_id = req.matches[1];
    std::string metric = GetStrParam(req, "metric");
    if (metric.empty()) {
        SendError(res, "metric required", 400, "INVALID_ARGUMENT", rid);
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
    std::string is_anomaly = GetStrParam(req, "is_anomaly");
    std::string anomaly_type = GetStrParam(req, "anomaly_type");
    std::string start_time = GetStrParam(req, "start_time");
    std::string end_time = GetStrParam(req, "end_time");

    bool debug = GetStrParam(req, "debug") == "true";
    try {
        auto start = std::chrono::steady_clock::now();
        auto data = db_client_->GetHistogram(run_id, metric, bins, min_val, max_val, is_anomaly, anomaly_type, start_time, end_time);
        auto end = std::chrono::steady_clock::now();
        data["request_id"] = rid;
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            long row_count = data.value("counts", nlohmann::json::array()).size();
            nlohmann::json resolved;
            resolved["metric"] = metric;
            resolved["bins"] = bins;
            resolved["min"] = min_val;
            resolved["max"] = max_val;
            data["debug"] = BuildDebugMeta(duration_ms, row_count, resolved);
        }
        SendJson(res, data);
    } catch (const std::invalid_argument& e) {
        SendError(res, e.what(), 400, "INVALID_ARGUMENT", rid);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleTrainModel(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    try {
        auto j = nlohmann::json::parse(req.body);
        std::string dataset_id = j.at("dataset_id");
        std::string name = j.value("name", "pca_default");

        // 1. Create DB entry
        std::string model_run_id = db_client_->CreateModelRun(dataset_id, name);
        if (model_run_id.empty()) {
            SendError(res, "Failed to create model run in DB", 500);
            return;
        }

        // 2. Spawn training via JobManager
        job_manager_->StartJob("train-" + model_run_id, [this, model_run_id, dataset_id]() {
            spdlog::info("Training started for model {}", model_run_id);
            db_client_->UpdateModelRunStatus(model_run_id, "RUNNING");

            std::string output_dir = "artifacts/pca/" + model_run_id;
            std::string output_path = output_dir + "/model.json";

            try {
                std::filesystem::create_directories(output_dir);
                auto artifact = telemetry::training::TrainPcaFromDb(db_conn_str_, dataset_id, 3, 99.5);
                telemetry::training::WriteArtifactJson(artifact, output_path);

                spdlog::info("Training successful for model {}", model_run_id);
                db_client_->UpdateModelRunStatus(model_run_id, "COMPLETED", output_path);
            } catch (const std::exception& e) {
                spdlog::error("Training failed for model {}: {}", model_run_id, e.what());
                db_client_->UpdateModelRunStatus(model_run_id, "FAILED", "", e.what());
                throw; // JobManager will catch and log it too
            }
        });

        nlohmann::json resp;
        resp["model_run_id"] = model_run_id;
        resp["status"] = "PENDING";
        resp["request_id"] = rid;
        SendJson(res, resp, 202);
    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what(), 400, "BAD_REQUEST", rid);
    }
}

void ApiServer::HandleGetTrainStatus(const httplib::Request& req, httplib::Response& res) {
    std::string model_run_id = req.matches[1];
    auto j = db_client_->GetModelRun(model_run_id);
    if (j.empty()) {
        SendError(res, "Model run not found", 404);
    } else {
        SendJson(res, j);
    }
}

void ApiServer::HandleListModels(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
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
        resp["request_id"] = rid;
        SendJson(res, resp);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleGetModelDetail(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string model_run_id = req.matches[1];
    try {
        auto j = db_client_->GetModelRun(model_run_id);
        if (j.empty()) {
            SendError(res, "Model run not found", 404, "NOT_FOUND", rid);
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
        j["request_id"] = rid;
        SendJson(res, j);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleInference(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    try {
        auto start = std::chrono::steady_clock::now();
        auto j = nlohmann::json::parse(req.body);
        std::string model_run_id = j.at("model_run_id");
        auto samples = j.at("samples");

        // 1. Get Model Info
        auto model_info = db_client_->GetModelRun(model_run_id);
        if (model_info.empty()) {
            SendError(res, "Model not found", 404, "NOT_FOUND", rid);
            return;
        }

        std::string artifact_path = model_info.value("artifact_path", "");
        if (artifact_path.empty()) {
            SendError(res, "Model is not yet complete or has no artifact", 400, "BAD_REQUEST", rid);
            return;
        }

        // 2. Load Model
        telemetry::anomaly::PcaModel pca;
        try {
            pca.Load(artifact_path);
        } catch (const std::exception& e) {
            SendError(res, "Failed to load PCA model artifact: " + std::string(e.what()), 500, "LOAD_ERROR", rid);
            return;
        }

        // 3. Process Samples
        std::string inference_id = db_client_->CreateInferenceRun(model_run_id);
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

        nlohmann::json resp;
        resp["results"] = results;
        resp["model_run_id"] = model_run_id;
        resp["inference_id"] = inference_id;
        resp["anomaly_count"] = anomaly_count;
        resp["request_id"] = rid;
        SendJson(res, resp);

    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what(), 400, "BAD_REQUEST", rid);
    }
}

void ApiServer::HandleListInferenceRuns(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string model_run_id = GetStrParam(req, "model_run_id");
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
        resp["request_id"] = rid;
        SendJson(res, resp);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleGetInferenceRun(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string inference_id = req.matches[1];
    try {
        auto j = db_client_->GetInferenceRun(inference_id);
        if (j.empty()) {
            SendError(res, "Inference run not found", 404, "NOT_FOUND", rid);
            return;
        }
        j["request_id"] = rid;
        SendJson(res, j);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleListJobs(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    std::string status = GetStrParam(req, "status");
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string model_run_id = GetStrParam(req, "model_run_id");
    std::string created_from = GetStrParam(req, "created_from");
    std::string created_to = GetStrParam(req, "created_to");
    try {
        auto jobs = db_client_->ListScoreJobs(limit, offset, status, dataset_id, model_run_id, created_from, created_to);
        nlohmann::json resp;
        resp["items"] = jobs;
        resp["limit"] = limit;
        resp["offset"] = offset;
        resp["request_id"] = rid;
        SendJson(res, resp);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleScoreDatasetJob(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    try {
        auto j = nlohmann::json::parse(req.body);
        std::string dataset_id = j.at("dataset_id");
        std::string model_run_id = j.at("model_run_id");

        std::string job_id = db_client_->CreateScoreJob(dataset_id, model_run_id);
        if (job_id.empty()) {
            SendError(res, "Failed to create job", 500);
            return;
        }

        auto job = db_client_->GetScoreJob(job_id);
        if (job.empty()) {
            SendError(res, "Failed to load job status", 500);
            return;
        }

        std::string status = job.value("status", "");
        long total_rows = job.value("total_rows", 0L);
        long processed_rows = job.value("processed_rows", 0L);
        long last_record_id = job.value("last_record_id", 0L);

        if (status == "RUNNING" || status == "COMPLETED") {
            nlohmann::json resp;
            resp["job_id"] = job_id;
            resp["status"] = status;
            resp["total_rows"] = total_rows;
            resp["processed_rows"] = processed_rows;
            resp["last_record_id"] = last_record_id;
            SendJson(res, resp, 200);
            return;
        }

        job_manager_->StartJob("score-" + job_id, [this, dataset_id, model_run_id, job_id]() {
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
                while (true) {
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
                local_db.UpdateScoreJob(job_id, "COMPLETED", total, processed, last_record);
            } catch (const std::exception& e) {
                auto job_info = local_db.GetScoreJob(job_id);
                long total = job_info.value("total_rows", 0L);
                long processed = job_info.value("processed_rows", 0L);
                long last_record = job_info.value("last_record_id", 0L);
                local_db.UpdateScoreJob(job_id, "FAILED", total, processed, last_record, e.what());
                throw;
            }
        });

        nlohmann::json resp;
        resp["job_id"] = job_id;
        resp["status"] = "RUNNING";
        resp["request_id"] = rid;
        SendJson(res, resp, 202);
    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what(), 400, "BAD_REQUEST", rid);
    }
}

void ApiServer::HandleGetJobStatus(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string job_id = req.matches[1];
    try {
        auto j = db_client_->GetScoreJob(job_id);
        if (j.empty()) {
            SendError(res, "Job not found", 404, "NOT_FOUND", rid);
            return;
        }
        j["request_id"] = rid;
        SendJson(res, j);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleGetJobProgress(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string job_id = req.matches[1];
    try {
        auto j = db_client_->GetScoreJob(job_id);
        if (j.empty()) {
            SendError(res, "Job not found", 404, "NOT_FOUND", rid);
            return;
        }
        j["request_id"] = rid;
        SendJson(res, j);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleModelEval(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string model_run_id = req.matches[1];
    std::string dataset_id = GetStrParam(req, "dataset_id");
    int points = GetIntParam(req, "points", 50);
    int max_samples = GetIntParam(req, "max_samples", 20000);
    bool debug = GetStrParam(req, "debug") == "true";
    if (dataset_id.empty()) {
        SendError(res, "dataset_id required", 400, "BAD_REQUEST", rid);
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
        eval["request_id"] = rid;
        SendJson(res, eval);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::HandleModelErrorDistribution(const httplib::Request& req, httplib::Response& res) {
    std::string rid = GetRequestId(req);
    std::string model_run_id = req.matches[1];
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string group_by = GetStrParam(req, "group_by");
    bool debug = GetStrParam(req, "debug") == "true";
    if (dataset_id.empty() || group_by.empty()) {
        SendError(res, "dataset_id and group_by required", 400, "BAD_REQUEST", rid);
        return;
    }
    std::unordered_map<std::string, std::string> allowed = {
        {"anomaly_type", "h.anomaly_type"},
        {"region", "h.region"},
        {"project_id", "h.project_id"}
    };
    if (allowed.find(group_by) == allowed.end()) {
        SendError(res, "Invalid group_by", 400, "INVALID_ARGUMENT", rid);
        return;
    }
    try {
        auto start = std::chrono::steady_clock::now();
        auto dist = db_client_->GetErrorDistribution(dataset_id, model_run_id, allowed[group_by]);
        auto end = std::chrono::steady_clock::now();
        nlohmann::json resp;
        resp["items"] = dist;
        resp["request_id"] = rid;
        if (debug) {
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            nlohmann::json resolved;
            resolved["group_by"] = allowed[group_by];
            resp["debug"] = BuildDebugMeta(duration_ms, static_cast<long>(dist.size()), resolved);
        }
        SendJson(res, resp);
    } catch (const std::invalid_argument& e) {
        SendError(res, e.what(), 400, "INVALID_ARGUMENT", rid);
    } catch (const std::exception& e) {
        SendError(res, e.what(), 500, "DB_ERROR", rid);
    }
}

void ApiServer::SendJson(httplib::Response& res, const nlohmann::json& j, int status) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void ApiServer::SendError(httplib::Response& res, 
                        const std::string& msg, 
                        int status,
                        const std::string& code,
                        const std::string& request_id) {
    nlohmann::json j;
    j["error"]["message"] = msg;
    j["error"]["code"] = code;
    if (!request_id.empty()) {
        j["error"]["request_id"] = request_id;
    }
    SendJson(res, j, status);
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

} // namespace api
} // namespace telemetry
