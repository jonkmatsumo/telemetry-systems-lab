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

namespace telemetry {
namespace api {

class ApiServer {
public:
    ApiServer(const std::string& grpc_target, const std::string& db_conn_str);
    ~ApiServer();

    void Start(const std::string& host, int port);
    void Stop();

private:
    // Route Handlers
    void HandleGenerateData(const httplib::Request& req, httplib::Response& res);
    void HandleListDatasets(const httplib::Request& req, httplib::Response& res);
    void HandleGetDataset(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetSummary(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetTopK(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetTimeSeries(const httplib::Request& req, httplib::Response& res);
    void HandleDatasetHistogram(const httplib::Request& req, httplib::Response& res);
    void HandleTrainModel(const httplib::Request& req, httplib::Response& res);
    void HandleGetTrainStatus(const httplib::Request& req, httplib::Response& res);
    void HandleListModels(const httplib::Request& req, httplib::Response& res);
    void HandleGetModelDetail(const httplib::Request& req, httplib::Response& res);
    void HandleInference(const httplib::Request& req, httplib::Response& res);
    void HandleListInferenceRuns(const httplib::Request& req, httplib::Response& res);
    void HandleGetInferenceRun(const httplib::Request& req, httplib::Response& res);
    void HandleListJobs(const httplib::Request& req, httplib::Response& res);
    void HandleScoreDatasetJob(const httplib::Request& req, httplib::Response& res);
    void HandleGetJobStatus(const httplib::Request& req, httplib::Response& res);
    void HandleGetJobProgress(const httplib::Request& req, httplib::Response& res);
    void HandleModelEval(const httplib::Request& req, httplib::Response& res);
    void HandleModelErrorDistribution(const httplib::Request& req, httplib::Response& res);

    // Helpers
    void SendJson(httplib::Response& res, nlohmann::json j, int status = 200, const std::string& request_id = "");
    void SendError(httplib::Response& res, 
                   const std::string& msg, 
                   int status = 400,
                   const std::string& code = "INTERNAL_ERROR",
                   const std::string& request_id = "");
    static int GetIntParam(const httplib::Request& req, const std::string& key, int def);
    static double GetDoubleParam(const httplib::Request& req, const std::string& key, double def);
    static std::string GetStrParam(const httplib::Request& req, const std::string& key);

    httplib::Server svr_;
    std::unique_ptr<telemetry::TelemetryService::Stub> stub_;
    std::unique_ptr<DbClient> db_client_;
    std::unique_ptr<JobManager> job_manager_;
    
    std::string grpc_target_;
    std::string db_conn_str_;
};

} // namespace api
} // namespace telemetry
