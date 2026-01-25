#include <iostream>
#include <string>
#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include "server.h"

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    TelemetryServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    
    spdlog::info("Server listening on {}", server_address);
    server->Wait();
}

int main() {
    spdlog::info("Telemetry Generator Service Starting...");
    try {
        RunServer();
    } catch (const std::exception& e) {
        spdlog::error("Server failed: {}", e.what());
        return 1;
    }
    return 0;
}
