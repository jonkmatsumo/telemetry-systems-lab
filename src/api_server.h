#pragma once

#include <memory>
#include <string>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"
#include "db_client.h"
#include "job_manager.h"
#include "job_reconciler.h"
#include "pca_model_cache.h"
#include "training/pca_trainer.h"

namespace telemetry::api {

class ApiServer {
public:
    friend class ApiServerTestPeer;
    ApiServer(std::string grpc_target, std::string db_conn_str);
    ApiServer(std::string grpc_target, std::shared_ptr<IDbClient> db_client);
    ~ApiServer();

    void Start(const std::string& host, int port);
    void Stop();

private:
    struct TuningTask {
        std::string parent_run_id;
        std::string name;
        std::string dataset_id;
        std::string rid;
        std::vector<training::TrainingConfig> trials;
        int max_concurrency;
    };

    void OrchestrateTuning(TuningTask task);
    void Initialize();
    // Route Handlers
    void HandleGenerateData(const httplib::Request& req, httplib::Response& res);
    void HandleListDatasets(const httplib::Request& req, httplib::Response& res);
    void HandleGetDataset(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetSummary(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetTopK(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetTimeSeries(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetHistogram(const httplib::Request& req, httplib::Response& res);
    void HandleGetDatasetSamples(const httplib::Request& req, httplib::Response& res);
    void HandleGetDatasetRecord(const httplib::Request& req, httplib::Response& res);
    void HandleGetDatasetMetricStats(const httplib::Request& req, httplib::Response& res);
    void HandleGetDatasetMetricsSummary(const httplib::Request& req, httplib::Response& res);
    void HandleGetDatasetModels(const httplib::Request& req, httplib::Response& res);
    void HandleTrainModel(const httplib::Request& req, httplib::Response& res);
    void HandleGetTrainStatus(const httplib::Request& req, httplib::Response& res);
    void HandleListModels(const httplib::Request& req, httplib::Response& res);
    void HandleGetModelDetail(const httplib::Request& req, httplib::Response& res);
    void HandleGetModelScoredDatasets(const httplib::Request& req, httplib::Response& res);
    void HandleGetScores(const httplib::Request& req, httplib::Response& res);
    void HandleInference(const httplib::Request& req, httplib::Response& res);
    void HandleListInferenceRuns(const httplib::Request& req, httplib::Response& res);
    void HandleGetInferenceRun(const httplib::Request& req, httplib::Response& res);
    void HandleListJobs(const httplib::Request& req, httplib::Response& res);
    void HandleScoreDatasetJob(const httplib::Request& req, httplib::Response& res);
    void HandleGetJobStatus(const httplib::Request& req, httplib::Response& res);
    void HandleGetJobProgress(const httplib::Request& req, httplib::Response& res);
    void HandleModelEval(const httplib::Request& req, httplib::Response& res);
    void HandleModelErrorDistribution(const httplib::Request& req, httplib::Response& res);

    void RunPcaTraining(const std::string& model_run_id, 
                         const std::string& dataset_id, 
                         int n_components, 
                         double percentile, 
                         const std::string& rid);

    void ValidateRoutes();

    // Helpers
    void SendJson(httplib::Response& res, nlohmann::json j, int status = 200, const std::string& request_id = "");
    struct ApiErrorArgs {
        httplib::Response& res;
        std::string message;
        int status = 400;
        std::string code = "E_INTERNAL";
        std::string request_id = "";
    };

    void SendError(const ApiErrorArgs& args);
    static auto GetIntParam(const httplib::Request& req, const std::string& key, int def) -> int;
    static auto GetDoubleParam(const httplib::Request& req, const std::string& key, double def) -> double;
    static auto GetStrParam(const httplib::Request& req, const std::string& key) -> std::string;

    httplib::Server svr_;
    std::unique_ptr<telemetry::TelemetryService::Stub> stub_;
    std::string grpc_target_;
    std::string db_conn_str_;
    std::shared_ptr<IDbClient> db_client_;
    std::shared_ptr<DbConnectionManager> db_manager_;
    std::unique_ptr<JobManager> job_manager_;
    std::unique_ptr<JobReconciler> job_reconciler_;
    std::unique_ptr<telemetry::anomaly::PcaModelCache> model_cache_;
};

} // namespace telemetry::api
