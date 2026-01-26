#include <iostream>
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "db_client.h"
#include "contract.h"
#include "preprocessing.h"
#include "detector_config.h"

#include <map>
#include "detectors/detector_a.h"

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
    // Tweak output for test visibility
    config.outliers.robust_z_threshold = 2.0; 

    Preprocessor preprocessor(config.preprocessing);
    
    // State: Map host_id -> DetectorA
    std::map<std::string, DetectorA> detectors;

    // Simulation Loop
    spdlog::info("Starting scoring loop (Simulation)...");
    
    // Create a detector for host-1
    detectors.emplace("host-1", DetectorA(config.window, config.outliers));

    // Simulate 100 points
    for (int i = 0; i < 100; ++i) {
        TelemetryRecord r;
        r.host_id = "host-1";
        r.cpu_usage = 50.0 + (i % 10); // Normal noise
        
        // Inject Anomaly at i=50
        if (i == 50) {
            r.cpu_usage = 200.0; // Spike
            spdlog::warn("Injecting CPU anomaly at i=50");
        }

        r.memory_usage = 60.0;
        r.disk_utilization = 30.0;
        r.network_rx_rate = 100.0;
        r.network_tx_rate = 50.0;

        // 1. Vectorize
        FeatureVector vec = FeatureVector::FromRecord(r);

        // 2. Preprocess
        preprocessor.Apply(vec);

        // 3. Detect
        if (detectors.find(r.host_id) != detectors.end()) {
            auto& detector = detectors.at(r.host_id);
            AnomalyScore score = detector.Update(vec);
            
            if (score.is_anomaly) {
                spdlog::info("[ANOMALY DETECTED] Host: {} Step: {} MaxZ: {:.2f} Details: {}", 
                    r.host_id, i, score.max_z_score, score.details);
            }
        }
    }

    spdlog::info("Scorer simulation complete.");
    return 0;
}
