#include "api_server.h"
#include <iostream>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

auto main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) -> int { // NOLINT(bugprone-exception-escape)
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);

    try {
        std::string grpc_target = "localhost:52051";
        if (const char* env_p = std::getenv("GRPC_GENERATOR_TARGET")) {
            grpc_target = env_p;
        }

        const char* db_conn_env = std::getenv("DB_CONNECTION_STRING");
        if (!db_conn_env || std::string(db_conn_env).empty()) {
            throw std::runtime_error("DB_CONNECTION_STRING environment variable is required");
        }
        std::string db_conn = db_conn_env;

        int port = 8280;
        if (const char* env_p = std::getenv("API_PORT")) {
            port = std::stoi(env_p);
        }

        telemetry::api::ApiServer server(grpc_target, db_conn);
        server.Start("0.0.0.0", port);
    } catch (const std::exception& e) {
        spdlog::error("Fatal error in API Server: {}", e.what());
        return 1;
    } catch (...) {
        spdlog::error("Unknown fatal error in API Server");
        return 1;
    }

    return 0;
}
