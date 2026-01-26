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
#include "metrics.h"

using namespace telemetry;
using namespace telemetry::anomaly;

int main(int argc, char** argv) {
    // Initialize logger
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::info("Telemetry Scorer v1.0.0 Starting...");

    // Argument Parsing (Simple)
    int shard_id = 0;
    int num_shards = 1;
    if (argc >= 3) {
        shard_id = std::stoi(argv[1]);
        num_shards = std::stoi(argv[2]);
    }
    spdlog::info("Sharding Config: Shard {} of {}", shard_id, num_shards);

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

    AlertManager alert_manager(1, 10); // Hysteresis 1, Cooldown 10s

// State Helper
    struct HostState {
        std::chrono::system_clock::time_point last_b_run;
    };
    std::map<std::string, HostState> host_states;

    // Enable Gating & Poisoning via Config Override for this phase
    config.outliers.enable_poison_mitigation = true;
    config.gating.enable_gating = true;
    config.gating.period_ms = 10000; // 10s for test visibility

    // Simulation Loop
    spdlog::info("Starting scoring loop (Simulation)...");
    
    // Create a detector for host-1
    detectors_a.emplace("host-1", DetectorA(config.window, config.outliers));
    detectors_a.emplace("host-2", DetectorA(config.window, config.outliers));
    
    auto start_time = std::chrono::system_clock::now();

    // Simulate 100 points, 1 second apart
    for (int i = 0; i < 100; ++i) {
        auto current_time = start_time + std::chrono::seconds(i);
        
        telemetry::metrics::MetricsRegistry::Instance().Increment("telemetry_records_total", 2);

        // Simulate multiple hosts
        std::vector<std::string> hosts = {"host-1", "host-2"};
        
        for (const auto& host : hosts) {
            // Sharding Check
            size_t h = std::hash<std::string>{}(host);
            if ((h % num_shards) != (size_t)shard_id) {
                continue; // Not my shard
            }

            TelemetryRecord r;
            r.host_id = host;
            r.run_id = "sim-run-001";
            r.metric_timestamp = current_time;
            
            // Base values
            r.cpu_usage = 50.0 + (i % 10); 
            r.memory_usage = 60.0;
            // Diff behavior for host-2 to see variety
            if (host == "host-2") r.cpu_usage += 20.0;

            r.disk_utilization = 30.0;
            r.network_rx_rate = 100.0;
            r.network_tx_rate = 50.0;

            // Inject Anomaly at i=50 (Spike) for host-1
            if (host == "host-1" && i == 50) {
                r.cpu_usage = 200.0; 
                spdlog::warn("[{}] Injecting CPU anomaly at i=50", host);
            }
            // Inject Anomaly for host-2 at i=60
            if (host == "host-2" && i == 60) {
                r.cpu_usage = 220.0;
                spdlog::warn("[{}] Injecting CPU anomaly at i=60", host);
            }
            
            // Inject PCA anomaly at i=70 (Correlation break)
            if (host == "host-1" && i == 70) {
                r.cpu_usage = 80.0; 
                r.network_rx_rate = 0.0; // Drop to zero
                spdlog::warn("[{}] Injecting Correlation anomaly at i=70", host);
            }

            // 1. Vectorize
            FeatureVector vec = FeatureVector::FromRecord(r);

            // 2. Preprocess
            preprocessor.Apply(vec);

            // 3. Detect A
            bool flag_a = false;
            double score_a = 0.0;
            std::string details_a;

            // Ensure detector exists (lazy init or pre-init)
            if (detectors_a.find(r.host_id) == detectors_a.end()) {
                 detectors_a.emplace(r.host_id, DetectorA(config.window, config.outliers));
            }

            // Detect A Latency
            auto t_a_start = std::chrono::high_resolution_clock::now();
            auto& detector = detectors_a.at(r.host_id);
            AnomalyScore score = detector.Update(vec);
            auto t_a_end = std::chrono::high_resolution_clock::now();
            telemetry::metrics::MetricsRegistry::Instance().RecordLatency("detector_a_latency_ms", std::chrono::duration<double, std::milli>(t_a_end - t_a_start).count());

            if (score.is_anomaly) {
                flag_a = true;
                score_a = score.max_z_score;
                details_a = score.details;
                spdlog::info("[DETECTOR A] Host: {} Step: {} Z: {:.2f} Details: {}", r.host_id, i, score_a, details_a);
                telemetry::metrics::MetricsRegistry::Instance().Increment("detector_a_anomalies_total");
            }
            
            // 4. Detect B (Gated)
            bool flag_b = false;
            double score_b = 0.0;
            std::string details_b;
            bool run_b = true; // Default true if no gating

            if (config.gating.enable_gating) {
                auto& h_state = host_states[host];
                bool triggered = flag_a; // Trigger if A saw anomaly
                
                auto ms_since = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - h_state.last_b_run).count();
                bool scheduled = ms_since >= config.gating.period_ms;

                if (!triggered && !scheduled) {
                    run_b = false;
                } else {
                    // Update last run if we run it due to schedule (or even trigger? usually yes)
                    h_state.last_b_run = current_time;
                }
            }

            if (run_b) {
                telemetry::metrics::MetricsRegistry::Instance().Increment("detector_b_evaluations_total");
                auto t_b_start = std::chrono::high_resolution_clock::now();
                PcaScore pca_res = pca_model.Score(vec);
                auto t_b_end = std::chrono::high_resolution_clock::now();
                telemetry::metrics::MetricsRegistry::Instance().RecordLatency("detector_b_latency_ms", std::chrono::duration<double, std::milli>(t_b_end - t_b_start).count());

                if (pca_res.is_anomaly) {
                    flag_b = true;
                    score_b = pca_res.reconstruction_error;
                    details_b = pca_res.details;
                    spdlog::info("[DETECTOR B] Host: {} Step: {} ReconErr: {:.2f} Details: {}", r.host_id, i, score_b, details_b);
                    telemetry::metrics::MetricsRegistry::Instance().Increment("detector_b_anomalies_total");
                }
            } else {
                // Not evaluated
                score_b = -1.0; 
            }

            // 4. Fuse & Alert
            std::string combined_details;
            if (flag_a) combined_details += "[A:" + details_a + "] ";
            if (run_b && flag_b) combined_details += "[B:" + details_b + "] ";
            if (!run_b) combined_details += "[B:SKIPPED] ";

            std::vector<Alert> alerts = alert_manager.Evaluate(
                r.host_id, r.run_id, r.metric_timestamp,
                flag_a, score_a, 
                flag_b, score_b, 
                combined_details
            );

            for (const auto& alert : alerts) {
                telemetry::metrics::MetricsRegistry::Instance().Increment("alerts_total");
                spdlog::error(">>> [ALERT GENERATED] Host: {} Severity: {} Source: {} Score: {:.2f}", 
                    alert.host_id, alert.severity, alert.source, alert.score);
            }
        } // End host loop
    } // End time loop
    
    spdlog::info(telemetry::metrics::MetricsRegistry::Instance().Dump());

    spdlog::info("Scorer simulation complete.");
    return 0;
}
