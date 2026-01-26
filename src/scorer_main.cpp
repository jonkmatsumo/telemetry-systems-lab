#include <iostream>
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "db_client.h"
#include "contract.h"
#include "preprocessing.h"
#include "detector_config.h"

using namespace telemetry;
using namespace telemetry::anomaly;

int main(int argc, char** argv) {
    // Initialize logger
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::info("Telemetry Scorer v1.0.0 Starting...");

    // Default Config
    DetectorConfig config;
    config.preprocessing.log1p_network = false;

    Preprocessor preprocessor(config.preprocessing);

    // DB Connection (using environment variables ideally, hardcoded for MVP plumbing)
    // In real app, we might use a dedicated Reader or stream.
    // For Phase 0, we just want to prove we can link and build.
    spdlog::info("Simulating DB connection...");

    // TODO: Connect IDbClient (requires factoring out factory or just using DbClient directly)
    // For now, let's just instantiate a dummy loop to prove the contract logic works in a main loop.
    
    // Simulate a stream of records
    spdlog::info("Starting scoring loop...");
    
    // Mock record
    TelemetryRecord r;
    r.host_id = "host-1";
    r.cpu_usage = 45.5;
    r.memory_usage = 60.2;
    r.disk_utilization = 30.0;
    r.network_rx_rate = 100.0;
    r.network_tx_rate = 50.0;
    r.metric_timestamp = std::chrono::system_clock::now();

    // Vectorize
    FeatureVector vec = FeatureVector::FromRecord(r);

    // Preprocess
    preprocessor.Apply(vec);

    spdlog::info("Processed record for {}: Vector=[{}, {}, {}, {}, {}]", 
        r.host_id, 
        vec.cpu_usage(), vec.memory_usage(), vec.disk_utilization(), 
        vec.network_rx_rate(), vec.network_tx_rate());

    spdlog::info("Scorer plumbing complete. Waiting for termination...");
    // Just exit for MVP test
    return 0;
}
