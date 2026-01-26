#pragma once

#include <memory>
#include <string>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"
#include "db_client.h"

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
    void HandleGetDataset(const httplib::Request& req, httplib::Response& res);
    void HandleTrainModel(const httplib::Request& req, httplib::Response& res);
    void HandleGetTrainStatus(const httplib::Request& req, httplib::Response& res);
    void HandleInference(const httplib::Request& req, httplib::Response& res);

    // Helpers
    void SendJson(httplib::Response& res, const nlohmann::json& j, int status = 200);
    void SendError(httplib::Response& res, const std::string& msg, int status = 400);

    httplib::Server svr_;
    std::unique_ptr<telemetry::TelemetryService::Stub> stub_;
    std::unique_ptr<DbClient> db_client_;
    
    std::string grpc_target_;
};

} // namespace api
} // namespace telemetry
