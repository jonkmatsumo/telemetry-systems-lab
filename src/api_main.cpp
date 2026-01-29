#include "api_server.h"
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char** argv) {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);

    std::string grpc_target = "localhost:52051";
    if (const char* env_p = std::getenv("GRPC_GENERATOR_TARGET")) {
        grpc_target = env_p;
    }

    std::string db_conn = "postgresql://postgres:password@localhost:5432/telemetry";
    if (const char* env_p = std::getenv("DB_CONNECTION_STRING")) {
        db_conn = env_p;
    }

    int port = 8280;
    if (const char* env_p = std::getenv("API_PORT")) {
        port = std::stoi(env_p);
    }

    telemetry::api::ApiServer server(grpc_target, db_conn);
    
    try {
        server.Start("0.0.0.0", port);
    } catch (const std::exception& e) {
        spdlog::error("Fatal error in API Server: {}", e.what());
        return 1;
    }

    return 0;
}
