#include "api_server.h"
#include <spdlog/spdlog.h>
#include <thread>
#include "detectors/detector_a.h"
#include "detectors/pca_model.h"
#include "preprocessing.h"
#include "detector_config.h"

namespace telemetry {
namespace api {

ApiServer::ApiServer(const std::string& grpc_target, const std::string& db_conn_str)
    : grpc_target_(grpc_target) 
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

    svr_.Get("/datasets/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDataset(req, res);
    });

    svr_.Post("/train", [this](const httplib::Request& req, httplib::Response& res) {
        HandleTrainModel(req, res);
    });

    svr_.Get("/train/:id", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetTrainStatus(req, res);
    });

    svr_.Post("/inference", [this](const httplib::Request& req, httplib::Response& res) {
        HandleInference(req, res);
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

void ApiServer::HandleGetDataset(const httplib::Request& req, httplib::Response& res) {
    std::string run_id = req.path_params.at("id");
    
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
        SendJson(res, resp);
    } else {
        SendError(res, "gRPC Error: " + status.error_message(), 404);
    }
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
            std::string cmd = "python3 python/training/train_pca.py --dataset_id " + dataset_id + 
                              " --output_dir " + output_dir;
            
            spdlog::info("Executing training command: {}", cmd);
            int ret = std::system(cmd.c_str());

            if (ret == 0) {
                spdlog::info("Training successful for model {}", model_run_id);
                db_client_->UpdateModelRunStatus(model_run_id, "COMPLETED", output_dir + "/model.json");
            } else {
                spdlog::error("Training failed for model {} with exit code {}", model_run_id, ret);
                db_client_->UpdateModelRunStatus(model_run_id, "FAILED", "", "Process exited with code " + std::to_string(ret));
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
    std::string model_run_id = req.path_params.at("id");
    auto j = db_client_->GetModelRun(model_run_id);
    if (j.empty()) {
        SendError(res, "Model run not found", 404);
    } else {
        SendJson(res, j);
    }
}

void ApiServer::HandleInference(const httplib::Request& req, httplib::Response& res) {
    try {
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

        if (!inference_id.empty()) {
            db_client_->UpdateInferenceRunStatus(inference_id, "COMPLETED", anomaly_count, results);
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

void ApiServer::SendJson(httplib::Response& res, const nlohmann::json& j, int status) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void ApiServer::SendError(httplib::Response& res, const std::string& msg, int status) {
    nlohmann::json j;
    j["error"] = msg;
    SendJson(res, j, status);
}

} // namespace api
} // namespace telemetry
