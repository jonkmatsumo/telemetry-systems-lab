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
#include "detectors/pca_model.h"
#include "alert_manager.h"

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
    config.outliers.robust_z_threshold = 3.0; 

    Preprocessor preprocessor(config.preprocessing);
    
    // State
    std::map<std::string, DetectorA> detectors_a;
    
    PcaModel pca_model;
    try {
        pca_model.Load("artifacts/pca/default/model.json");
    } catch (const std::exception& e) {
        spdlog::warn("Could not load PCA model, PCA detection will be disabled: {}", e.what());
    }

    AlertManager alert_manager(2, 60); // Hysteresis 2, Cooldown 60s

    // Simulation Loop
    spdlog::info("Starting scoring loop (Simulation)...");
    
    // Create a detector for host-1
    detectors_a.emplace("host-1", DetectorA(config.window, config.outliers));

    auto start_time = std::chrono::system_clock::now();

    // Simulate 100 points, 1 second apart
    for (int i = 0; i < 100; ++i) {
        auto current_time = start_time + std::chrono::seconds(i);

        TelemetryRecord r;
        r.host_id = "host-1";
        r.run_id = "sim-run-001";
        r.metric_timestamp = current_time;
        
        // Base values
        r.cpu_usage = 50.0 + (i % 10); 
        r.memory_usage = 60.0;
        r.disk_utilization = 30.0;
        r.network_rx_rate = 100.0;
        r.network_tx_rate = 50.0;

        // Inject Anomaly at i=50 (Spike)
        if (i == 50) {
            r.cpu_usage = 200.0; 
            spdlog::warn("Injecting CPU anomaly at i=50");
        }
        
        // Inject PCA anomaly at i=70 (Correlation break)
        // High CPU but Low Network (unusual if they are correlated)
        if (i == 70) {
            r.cpu_usage = 80.0; 
            r.network_rx_rate = 0.0; // Drop to zero
            spdlog::warn("Injecting Correlation anomaly at i=70");
        }

        // 1. Vectorize
        FeatureVector vec = FeatureVector::FromRecord(r);

        // 2. Preprocess
        preprocessor.Apply(vec);

        // 3. Detect
        bool flag_a = false;
        double score_a = 0.0;
        std::string details_a;

        if (detectors_a.find(r.host_id) != detectors_a.end()) {
            auto& detector = detectors_a.at(r.host_id);
            AnomalyScore score = detector.Update(vec);
            if (score.is_anomaly) {
                flag_a = true;
                score_a = score.max_z_score;
                details_a = score.details;
                spdlog::info("[DETECTOR A] Step: {} Z: {:.2f} Details: {}", i, score_a, details_a);
            }
        }

        bool flag_b = false;
        double score_b = 0.0;
        std::string details_b;

        PcaScore pca_res = pca_model.Score(vec);
        if (pca_res.is_anomaly) {
            flag_b = true;
            score_b = pca_res.reconstruction_error;
            details_b = pca_res.details;
            spdlog::info("[DETECTOR B] Step: {} ReconErr: {:.2f} Details: {}", i, score_b, details_b);
        }

        // 4. Fuse & Alert
        std::string combined_details;
        if (flag_a) combined_details += "[A:" + details_a + "] ";
        if (flag_b) combined_details += "[B:" + details_b + "] ";

        std::vector<Alert> alerts = alert_manager.Evaluate(
            r.host_id, r.run_id, r.metric_timestamp,
            flag_a, score_a, 
            flag_b, score_b, 
            combined_details
        );

        for (const auto& alert : alerts) {
            spdlog::error(">>> [ALERT GENERATED] Severity: {} Source: {} Score: {:.2f}", 
                alert.severity, alert.source, alert.score);
        }
    }

    spdlog::info("Scorer simulation complete.");
    return 0;
}
