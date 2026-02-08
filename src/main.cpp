#include <iostream>
#include <string>
#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include "server.h"

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    
    const char* db_conn_env = std::getenv("DB_CONNECTION_STRING");
    std::string db_conn_str = db_conn_env ? db_conn_env : "postgresql://postgres:password@localhost:5432/telemetry";
    
    TelemetryServiceImpl service(db_conn_str);

    grpc::ServerBuilder builder;

    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    
    spdlog::info("Server listening on {}", server_address);
    server->Wait();
}

auto main() -> int {
    spdlog::info("Telemetry Generator Service Starting...");
    try {
        RunServer();
    } catch (const std::exception& e) {
        spdlog::error("Server failed: {}", e.what());
        return 1;
    }
    return 0;
}
