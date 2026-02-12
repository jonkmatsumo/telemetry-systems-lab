#include <iostream>
#include <string>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include "server.h"

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    
    const char* db_conn_env = std::getenv("DB_CONNECTION_STRING");
    if (!db_conn_env || std::string(db_conn_env).empty()) {
        throw std::runtime_error("DB_CONNECTION_STRING environment variable is required");
    }
    std::string db_conn_str = db_conn_env;
    
    TelemetryServiceImpl service(db_conn_str);

    grpc::ServerBuilder builder;

    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    
    spdlog::info("Server listening on {}", server_address);
    server->Wait();
}

auto main() -> int { // NOLINT(bugprone-exception-escape)
    spdlog::info("Telemetry Generator Service Starting...");
    try {
        RunServer();
    } catch (const std::exception& e) {
        spdlog::error("Server failed: {}", e.what());
        return 1;
    }
    return 0;
}
