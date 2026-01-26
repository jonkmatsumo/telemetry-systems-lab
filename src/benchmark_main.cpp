#include <iostream>
#include <vector>
#include <chrono>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "contract.h"
#include "preprocessing.h"
#include "detector_config.h"
#include "detectors/detector_a.h"
#include "detectors/pca_model.h"
#include "alert_manager.h"

using namespace telemetry;
using namespace telemetry::anomaly;

// Mock Data Generator
std::vector<TelemetryRecord> GenerateMockData(int count) {
    std::vector<TelemetryRecord> records;
    records.reserve(count);
    for (int i = 0; i < count; ++i) {
        TelemetryRecord r;
        r.host_id = "bench-host-1";
        r.run_id = "bench-run";
        r.metric_timestamp = std::chrono::system_clock::now();
        r.cpu_usage = 50.0 + (i % 20); 
        r.memory_usage = 60.0;
        r.disk_utilization = 30.0;
        r.network_rx_rate = 100.0;
        r.network_tx_rate = 50.0;
        records.push_back(r);
    }
    return records;
}

int main(int argc, char** argv) {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);

    spdlog::info("Starting Telemetry Benchmark...");

    int N = 100000;
    if (argc > 1) {
        N = std::stoi(argv[1]);
    }
    spdlog::info("Generating {} mock records...", N);
    auto records = GenerateMockData(N);

    // Setup Stack
    DetectorConfig config;
    config.preprocessing.log1p_network = false;
    Preprocessor preprocessor(config.preprocessing);
    
    // We reuse one detector instance for the benchmark (simulating single host high throughput)
    DetectorA detector_a(config.window, config.outliers);
    
    PcaModel pca_model;
    try {
        pca_model.Load("artifacts/pca/default/model.json");
    } catch (...) {
        spdlog::warn("PCA model not found, skipping PCA load validation (will rely on empty model check)");
    }

    AlertManager alert_manager;

    spdlog::info("Benchmark loop starting...");
    auto start = std::chrono::high_resolution_clock::now();

    long anomalies_found = 0;

    for (const auto& r : records) {
        // 1. Vectorize
        FeatureVector vec = FeatureVector::FromRecord(r);

        // 2. Preprocess
        preprocessor.Apply(vec);

        // 3. Detect A
        bool flag_a = false;
        double score_a = 0.0;
        auto res_a = detector_a.Update(vec);
        if (res_a.is_anomaly) {
            flag_a = true;
            score_a = res_a.max_z_score;
        }

        // 4. Detect B
        bool flag_b = false;
        double score_b = 0.0;
        if (pca_model.IsLoaded()) {
            auto res_b = pca_model.Score(vec);
            if (res_b.is_anomaly) {
                flag_b = true;
                score_b = res_b.reconstruction_error;
            }
        }

        // 5. Fuse
        // Only forming strings if needed to save time? No, let's include string cost as it's part of the app
        std::string details = "bench"; 
        auto alerts = alert_manager.Evaluate(
            r.host_id, r.run_id, r.metric_timestamp,
            flag_a, score_a, 
            flag_b, score_b, 
            details
        );
        
        if (!alerts.empty()) anomalies_found++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    double throughput = N / diff.count();
    
    spdlog::info("Benchmark Complete.");
    spdlog::info("Processed {} records in {:.4f} s", N, diff.count());
    spdlog::info("Throughput: {:.2f} records/sec", throughput);
    spdlog::info("Anomalies Found (Alerts): {}", anomalies_found);

    return 0;
}
