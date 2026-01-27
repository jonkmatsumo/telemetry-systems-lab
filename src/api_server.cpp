#include "detectors/pca_model.h"
#include "api_server.h"
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

namespace telemetry {
namespace api {

ApiServer::ApiServer(const std::string& grpc_target, const std::string& db_conn_str)
    : grpc_target_(grpc_target), db_conn_str_(db_conn_str)
{
    // Initialize gRPC Stub
    auto channel = grpc::CreateChannel(grpc_target, grpc::InsecureChannelCredentials());
    stub_ = telemetry::TelemetryService::NewStub(channel);

    // Initialize DB Client
    db_client_ = std::make_unique<DbClient>(db_conn_str);

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

    svr_.Get("/jobs/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetJobStatus(req, res);
    });

    svr_.Get("/models/:id/eval", [this](const httplib::Request& req, httplib::Response& res) {
        HandleModelEval(req, res);
    });

    svr_.Get("/models/:id/error_distribution", [this](const httplib::Request& req, httplib::Response& res) {
        HandleModelErrorDistribution(req, res);
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
            SendJson(res, resp, 202);
        } else {
            SendError(res, "gRPC Error: " + status.error_message(), 500);
        }
    } catch (const std::exception& e) {
        SendError(res, std::string("JSON Parse Error: ") + e.what());
    }
}

void ApiServer::HandleListDatasets(const httplib::Request& req, httplib::Response& res) {
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    auto runs = db_client_->ListGenerationRuns(limit, offset);
    nlohmann::json resp;
    resp["items"] = runs;
    resp["limit"] = limit;
    resp["offset"] = offset;
    SendJson(res, resp);
}

void ApiServer::HandleGetDataset(const httplib::Request& req, httplib::Response& res) {
    std::string run_id = req.matches[1];
    auto detail = db_client_->GetDatasetDetail(run_id);
    if (!detail.empty()) {
        SendJson(res, detail);
        return;
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
        SendJson(res, resp);
    } else {
        SendError(res, "gRPC Error: " + status.error_message(), 404);
    }
}

void ApiServer::HandleDatasetSummary(const httplib::Request& req, httplib::Response& res) {
    std::string run_id = req.matches[1];
    int topk = GetIntParam(req, "topk", 5);
    auto summary = db_client_->GetDatasetSummary(run_id, topk);
    if (summary.empty()) {
        SendError(res, "Dataset not found", 404);
        return;
    }
    SendJson(res, summary);
}

void ApiServer::HandleDatasetTopK(const httplib::Request& req, httplib::Response& res) {
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
        SendError(res, "Invalid column", 400);
        return;
    }
    auto data = db_client_->GetTopK(run_id, allowed[column], k, is_anomaly, anomaly_type, start_time, end_time);
    nlohmann::json resp;
    resp["items"] = data;
    SendJson(res, resp);
}

void ApiServer::HandleDatasetTimeSeries(const httplib::Request& req, httplib::Response& res) {
    std::string run_id = req.matches[1];
    std::string metrics_param = GetStrParam(req, "metrics");
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
        SendError(res, "metrics required", 400);
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

    auto data = db_client_->GetTimeSeries(run_id, metrics, aggs, bucket_seconds, is_anomaly, anomaly_type, start_time, end_time);
    nlohmann::json resp;
    resp["items"] = data;
    resp["bucket_seconds"] = bucket_seconds;
    SendJson(res, resp);
}

void ApiServer::HandleDatasetHistogram(const httplib::Request& req, httplib::Response& res) {
    std::string run_id = req.matches[1];
    std::string metric = GetStrParam(req, "metric");
    if (metric.empty()) {
        SendError(res, "metric required", 400);
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

    auto data = db_client_->GetHistogram(run_id, metric, bins, min_val, max_val, is_anomaly, anomaly_type, start_time, end_time);
    SendJson(res, data);
}

void ApiServer::HandleTrainModel(const httplib::Request& req, httplib::Response& res) {
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

        // 2. Spawn training thread
        std::thread([this, model_run_id, dataset_id]() {
            spdlog::info("Training thread started for model {}", model_run_id);
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
            }
        }).detach();

        nlohmann::json resp;
        resp["model_run_id"] = model_run_id;
        resp["status"] = "PENDING";
        SendJson(res, resp, 202);

    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what());
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
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    auto models = db_client_->ListModelRuns(limit, offset);
    nlohmann::json resp;
    resp["items"] = models;
    resp["limit"] = limit;
    resp["offset"] = offset;
    SendJson(res, resp);
}

void ApiServer::HandleGetModelDetail(const httplib::Request& req, httplib::Response& res) {
    std::string model_run_id = req.matches[1];
    auto j = db_client_->GetModelRun(model_run_id);
    if (j.empty()) {
        SendError(res, "Model run not found", 404);
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
    SendJson(res, j);
}

void ApiServer::HandleInference(const httplib::Request& req, httplib::Response& res) {
    try {
        auto start = std::chrono::steady_clock::now();
        auto j = nlohmann::json::parse(req.body);
        std::string model_run_id = j.at("model_run_id");
        auto samples = j.at("samples");

        // 1. Get Model Info
        auto model_info = db_client_->GetModelRun(model_run_id);
        if (model_info.empty()) {
            SendError(res, "Model not found", 404);
            return;
        }

        std::string artifact_path = model_info.value("artifact_path", "");
        if (artifact_path.empty()) {
            SendError(res, "Model is not yet complete or has no artifact", 400);
            return;
        }

        // 2. Load Model
        telemetry::anomaly::PcaModel pca;
        try {
            pca.Load(artifact_path);
        } catch (const std::exception& e) {
            SendError(res, "Failed to load PCA model artifact: " + std::string(e.what()), 500);
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
        SendJson(res, resp);

    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what());
    }
}

void ApiServer::HandleListInferenceRuns(const httplib::Request& req, httplib::Response& res) {
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string model_run_id = GetStrParam(req, "model_run_id");
    int limit = GetIntParam(req, "limit", 50);
    int offset = GetIntParam(req, "offset", 0);
    auto runs = db_client_->ListInferenceRuns(dataset_id, model_run_id, limit, offset);
    nlohmann::json resp;
    resp["items"] = runs;
    resp["limit"] = limit;
    resp["offset"] = offset;
    SendJson(res, resp);
}

void ApiServer::HandleGetInferenceRun(const httplib::Request& req, httplib::Response& res) {
    std::string inference_id = req.matches[1];
    auto j = db_client_->GetInferenceRun(inference_id);
    if (j.empty()) {
        SendError(res, "Inference run not found", 404);
        return;
    }
    SendJson(res, j);
}

void ApiServer::HandleScoreDatasetJob(const httplib::Request& req, httplib::Response& res) {
    try {
        auto j = nlohmann::json::parse(req.body);
        std::string dataset_id = j.at("dataset_id");
        std::string model_run_id = j.at("model_run_id");

        std::string job_id = db_client_->CreateScoreJob(dataset_id, model_run_id);
        if (job_id.empty()) {
            SendError(res, "Failed to create job", 500);
            return;
        }

        std::thread([this, dataset_id, model_run_id, job_id]() {
            DbClient local_db(db_conn_str_);
            local_db.UpdateScoreJob(job_id, "RUNNING", 0, 0);
            try {
                auto model_info = local_db.GetModelRun(model_run_id);
                std::string artifact_path = model_info.value("artifact_path", "");
                if (artifact_path.empty()) {
                    throw std::runtime_error("Model artifact path missing");
                }
                telemetry::anomaly::PcaModel model;
                model.Load(artifact_path);

                pqxx::connection C(db_conn_str_);
                pqxx::nontransaction N(C);
                auto count_res = N.exec_params(
                    "SELECT COUNT(*) FROM host_telemetry_archival WHERE run_id = $1",
                    dataset_id);
                long total_rows = count_res.empty() ? 0 : count_res[0][0].as<long>();
                long processed = 0;
                const int batch = 5000;
                for (long offset = 0; offset < total_rows; offset += batch) {
                    auto rows = local_db.FetchScoringRows(dataset_id, offset, batch);
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
                    local_db.UpdateScoreJob(job_id, "RUNNING", total_rows, processed);
                    if (rows.empty()) break;
                }
                local_db.UpdateScoreJob(job_id, "COMPLETED", total_rows, processed);
            } catch (const std::exception& e) {
                local_db.UpdateScoreJob(job_id, "FAILED", 0, 0, e.what());
            }
        }).detach();

        nlohmann::json resp;
        resp["job_id"] = job_id;
        resp["status"] = "PENDING";
        SendJson(res, resp, 202);
    } catch (const std::exception& e) {
        SendError(res, std::string("Error: ") + e.what());
    }
}

void ApiServer::HandleGetJobStatus(const httplib::Request& req, httplib::Response& res) {
    std::string job_id = req.matches[1];
    auto j = db_client_->GetScoreJob(job_id);
    if (j.empty()) {
        SendError(res, "Job not found", 404);
        return;
    }
    SendJson(res, j);
}

void ApiServer::HandleModelEval(const httplib::Request& req, httplib::Response& res) {
    std::string model_run_id = req.matches[1];
    std::string dataset_id = GetStrParam(req, "dataset_id");
    int points = GetIntParam(req, "points", 50);
    int max_samples = GetIntParam(req, "max_samples", 20000);
    if (dataset_id.empty()) {
        SendError(res, "dataset_id required", 400);
        return;
    }
    auto eval = db_client_->GetEvalMetrics(dataset_id, model_run_id, points, max_samples);
    SendJson(res, eval);
}

void ApiServer::HandleModelErrorDistribution(const httplib::Request& req, httplib::Response& res) {
    std::string model_run_id = req.matches[1];
    std::string dataset_id = GetStrParam(req, "dataset_id");
    std::string group_by = GetStrParam(req, "group_by");
    if (dataset_id.empty() || group_by.empty()) {
        SendError(res, "dataset_id and group_by required", 400);
        return;
    }
    std::unordered_map<std::string, std::string> allowed = {
        {"anomaly_type", "h.anomaly_type"},
        {"region", "h.region"},
        {"project_id", "h.project_id"}
    };
    if (allowed.find(group_by) == allowed.end()) {
        SendError(res, "Invalid group_by", 400);
        return;
    }
    auto dist = db_client_->GetErrorDistribution(dataset_id, model_run_id, allowed[group_by]);
    nlohmann::json resp;
    resp["items"] = dist;
    SendJson(res, resp);
}

void ApiServer::SendJson(httplib::Response& res, const nlohmann::json& j, int status) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void ApiServer::SendError(httplib::Response& res, const std::string& msg, int status) {
    nlohmann::json j;
    j["error"] = msg;
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
