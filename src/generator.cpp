#include "generator.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <random>
#include <fmt/chrono.h>
#include <chrono> 
#include <ctime>
#include <iomanip>
#include <sstream>

// Helper to parse ISO string
std::chrono::system_clock::time_point ParseTime(const std::string& iso) {
    std::tm tm = {};
    std::stringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ"); // Expect Zulu for simplicity in MVP
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

Generator::Generator(const telemetry::GenerateRequest& request, 
                     std::string run_id, 
                     std::shared_ptr<DbClient> db_client)
    : config_(request), run_id_(run_id), db_(db_client) {
}

void Generator::InitializeHosts() {
    std::mt19937_64 rng(config_.seed());
    std::uniform_real_distribution<double> cpu_dist(10.0, 60.0); // Baseline averages
    std::uniform_real_distribution<double> phase_dist(0.0, 6.28);
    
    // Pick region (round robin or random)
    // Minimal set of regions if not provided
    std::vector<std::string> regions = {config_.regions().begin(), config_.regions().end()};
    if (regions.empty()) regions = {"us-east1", "us-west1", "eu-west1"};
    
    for (int i = 0; i < config_.host_count(); ++i) {
        HostProfile h;
        h.host_id = fmt::format("host-{}-{}", config_.tier(), i);
        h.project_id = "proj-" + config_.tier(); // keeping simple
        h.region = regions[i % regions.size()];
        h.cpu_base = cpu_dist(rng);
        h.mem_base = h.cpu_base * 0.8 + 10.0; // simple correlation for base
        h.phase_shift = phase_dist(rng);
        h.labels_json = fmt::format(R"({{"service": "backend", "tier": "{}"}})", config_.tier());
        hosts_.push_back(h);
    }
}

TelemetryRecord Generator::GenerateRecord(const HostProfile& host, 
                                          std::chrono::system_clock::time_point timestamp) {
    TelemetryRecord r;
    r.metric_timestamp = timestamp;
    r.run_id = run_id_;
    r.host_id = host.host_id;
    r.project_id = host.project_id;
    r.region = host.region;
    r.labels_json = host.labels_json;
    
    // Time since epoch in hours for seasonality
    auto duration = timestamp.time_since_epoch();
    double hours = std::chrono::duration_cast<std::chrono::seconds>(duration).count() / 3600.0;
    
    // Seasonality: Daily (24h) + Weekly (168h)
    double daily = 10.0 * std::sin((2 * M_PI * hours / 24.0) + host.phase_shift);
    double weekly = 5.0 * std::sin((2 * M_PI * hours / 168.0));
    
    // Deterministic noise using a hash of host+timestamp as seed for local randomness
    // Simpler: Just use a standard rng passed in? For now, let's keep it simple with std::rand (bad for dist but ok for MVP)
    // Better: LCG based on seed + point index.
    
    double noise = (rand() % 200 - 100) / 10.0; // +/- 10%
    
    r.cpu_usage = std::max(0.0, std::min(100.0, host.cpu_base + daily + weekly + noise));
    
    // Correlation: Mem follows CPU 
    r.memory_usage = std::max(0.0, std::min(100.0, r.cpu_usage * 0.7 + 20.0 + (rand() % 50 - 25)/10.0));
    
    r.disk_utilization = 30.0 + (rand() % 200 - 100)/20.0; 
    
    // RX/TX
    r.network_rx_rate = std::max(0.0, 10.0 + (daily/2.0) + (rand() % 100)/10.0);
    r.network_tx_rate = r.network_rx_rate * 0.8 + (rand() % 50)/10.0;
    
    // Ingestion Lag
    // Use fixed lag from config or default 2000ms
    int lag_ms = config_.timing_config().fixed_lag_ms();
    if (lag_ms == 0) lag_ms = 2000;
    
    // Add jitter (simple uniform for MVP, lognormal in full impl)
    int jitter = rand() % 500;
    
    r.ingestion_time = timestamp + std::chrono::milliseconds(lag_ms + jitter);
    
    return r;
}

void Generator::Run() {
    spdlog::info("Starting generation run {}", run_id_);
    try {
        db_->CreateRun(run_id_, config_, "RUNNING");
        
        InitializeHosts();
        
        auto start = ParseTime(config_.start_time_iso());
        auto end = ParseTime(config_.end_time_iso());
        auto duration = std::chrono::seconds(config_.interval_seconds());
        if (duration.count() == 0) duration = std::chrono::seconds(600); // default 10m
        
        long total_rows = 0;
        std::vector<TelemetryRecord> batch;
        const int BATCH_SIZE = 5000;
        
        for (auto t = start; t < end; t += duration) {
            for (const auto& host : hosts_) {
                batch.push_back(GenerateRecord(host, t));
                if (batch.size() >= BATCH_SIZE) {
                    db_->BatchInsertTelemetry(batch);
                    total_rows += batch.size();
                    db_->UpdateRunStatus(run_id_, "RUNNING", total_rows);
                    batch.clear();
                }
            }
        }
        
        // Final batch
        if (!batch.empty()) {
            db_->BatchInsertTelemetry(batch);
            total_rows += batch.size();
        }
        
        spdlog::info("Generation run {} complete. Total rows: {}", run_id_, total_rows);
        db_->UpdateRunStatus(run_id_, "SUCCEEDED", total_rows);
        
    } catch (const std::exception& e) {
        spdlog::error("Generation run {} failed: {}", run_id_, e.what());
        db_->UpdateRunStatus(run_id_, "FAILED", 0, e.what());
    }
}
