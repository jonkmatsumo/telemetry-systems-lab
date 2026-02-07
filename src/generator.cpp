#include "generator.h"
#include <spdlog/spdlog.h>
#include "obs/metrics.h"
#include "obs/context.h"
#include "obs/error_codes.h"
#include "obs/logging.h"
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
    tm.tm_isdst = 0; // Explicitly disable DST for UTC
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

Generator::Generator(const telemetry::GenerateRequest& request, 
                     std::string run_id, 
                     std::shared_ptr<IDbClient> db_client)
    : config_(request), run_id_(run_id), db_(db_client), rng_(static_cast<unsigned long long>(request.seed())) {
}


void Generator::InitializeHosts() {
    // Make a copy of the RNG for init so we don't advance the main sequence
    // Or just use the main RNG. Let's use the main RNG for consistency.
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
        h.region = regions[static_cast<size_t>(i) % regions.size()];
        h.cpu_base = cpu_dist(rng_);
        h.mem_base = h.cpu_base * 0.8 + 10.0; // simple correlation for base
        h.phase_shift = phase_dist(rng_);
        h.labels_json = fmt::format(R"({{"service": "backend", "tier": "{}"}})", config_.tier());
        hosts_.push_back(h);
    }
}

TelemetryRecord Generator::GenerateRecord(const HostProfile& host, 
                                          std::chrono::system_clock::time_point timestamp) {
    // Mutable host state requires passing by non-const reference or managing state elsewhere.
    // Since we are iterating, let's cast away constness or update the vector in the loop.
    // For MVP, we'll do the latter in the calling loop or just accept the const_cast for state updates 
    // (dirty but keeps signature simple for now).
    HostProfile& mutable_host = const_cast<HostProfile&>(host);

    TelemetryRecord r;
    r.metric_timestamp = timestamp;
    r.run_id = run_id_;
    r.host_id = host.host_id;
    r.project_id = host.project_id;
    r.region = host.region;
    r.labels_json = host.labels_json;
    
    // Time since epoch in hours for seasonality
    auto duration = timestamp.time_since_epoch();
    double hours = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(duration).count()) / 3600.0;
    
    // Seasonality
    double daily = 10.0 * std::sin((2 * M_PI * hours / 24.0) + host.phase_shift);
    double weekly = 5.0 * std::sin((2 * M_PI * hours / 168.0));
    
    std::uniform_real_distribution<double> noise_dist(-10.0, 10.0);
    double noise = noise_dist(rng_);

    
    double cpu = host.cpu_base + daily + weekly + noise;
    
    // Anomaly Probability Checks
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    double p = prob_dist(rng_);
    bool is_anomaly = false;
    std::string type;

    // 1. Collective / Burst Anomaly (Stateful)
    // If not already in burst, check start probability
    if (mutable_host.burst_remaining > 0) {
        mutable_host.burst_remaining--;
        cpu += 40.0; // Sustained load
        is_anomaly = true;
        type = "COLLECTIVE_BURST";
    } else if (config_.has_anomaly_config() && p < config_.anomaly_config().collective_rate()) {
        mutable_host.burst_remaining = config_.anomaly_config().burst_duration_points();
        if (mutable_host.burst_remaining == 0) mutable_host.burst_remaining = 5; // default
        cpu += 40.0;
        is_anomaly = true;
        type = "COLLECTIVE_BURST";
    }

    // 2. Correlation Break (Stateful)
    if (mutable_host.correlation_break_remaining > 0) {
        mutable_host.correlation_break_remaining--;
        mutable_host.correlation_broken = true;
        is_anomaly = true;
        type = "CORRELATION_BREAK";
    } else if (config_.has_anomaly_config() && p < config_.anomaly_config().correlation_break_rate()) {
         mutable_host.correlation_break_remaining = 5;
         mutable_host.correlation_broken = true;
         is_anomaly = true;
         type = "CORRELATION_BREAK";
    } else {
        mutable_host.correlation_broken = false;
    }

    // 3. Contextual Anomaly (Time based: 1AM-5AM UTC)
    // Simple check: hours mod 24. Assuming simplified UTC.
    int hour_of_day = static_cast<int>(static_cast<long>(hours) % 24);
    // Check constraint if provided in config, otherwise default 1-5am logic check only if rate > 0
    if (config_.has_anomaly_config() && config_.anomaly_config().contextual_rate() > 0) {
        // Only trigger if random check passes AND we are in the window
        double p_ctx = prob_dist(rng_);
        if (hour_of_day >= 1 && hour_of_day <= 5 && p_ctx < config_.anomaly_config().contextual_rate()) {
             std::uniform_real_distribution<double> spike_dist(0.0, 10.0);
             cpu = 90.0 + spike_dist(rng_); // Pin high
             is_anomaly = true;
             type = (type.empty() ? "CONTEXTUAL" : type + ",CONTEXTUAL");
        }
    }

    // 4. Point Spike (Transient)
    if (config_.has_anomaly_config() && p < config_.anomaly_config().point_rate()) {
        cpu += 50.0;
        is_anomaly = true;
        type = (type.empty() ? "POINT_SPIKE" : type + ",POINT_SPIKE");
    }

    // Clamp CPU
    r.cpu_usage = std::max(0.0, std::min(100.0, cpu));
    
    // Derived Metrics
    // Memory follows CPU unless Correlation Break
    if (mutable_host.correlation_broken) {
        // Inverse or decoupled
        r.memory_usage = std::max(0.0, std::min(100.0, 100.0 - r.cpu_usage + noise));
    } else {
         std::uniform_real_distribution<double> mem_noise(-2.5, 2.5);
         r.memory_usage = std::max(0.0, std::min(100.0, r.cpu_usage * 0.7 + 20.0 + mem_noise(rng_)));
    }
    
    std::uniform_real_distribution<double> disk_noise(-5.0, 5.0);
    r.disk_utilization = 30.0 + disk_noise(rng_); 
    
    // RX/TX
    std::uniform_real_distribution<double> net_node(0.0, 10.0);
    r.network_rx_rate = std::max(0.0, 10.0 + (daily/2.0) + net_node(rng_));
    // Correlation break could also affect Network
    if (mutable_host.correlation_broken) {
         r.network_tx_rate = 1.0; // Data sink (high RX, low TX)
         r.network_rx_rate += 50.0; // DDoS simulation
    } else {
         std::uniform_real_distribution<double> net_jitter(0.0, 5.0);
         r.network_tx_rate = r.network_rx_rate * 0.8 + net_jitter(rng_);
    }
    
    r.is_anomaly = is_anomaly;
    r.anomaly_type = type;

    // Ingestion Lag
    // Use fixed lag from config or default 2000ms
    int lag_ms = config_.timing_config().fixed_lag_ms();
    if (lag_ms == 0) lag_ms = 2000;
    
    // Add jitter (simple uniform for MVP, lognormal in full impl)
    std::uniform_int_distribution<int> jitter_dist(0, 500);
    int jitter = jitter_dist(rng_);

    
    r.ingestion_time = timestamp + std::chrono::milliseconds(lag_ms + jitter);
    
    return r;
}


void Generator::Run() {
    spdlog::info("Starting generation run {} (req_id: {})", run_id_, config_.request_id());
    auto start_time = std::chrono::steady_clock::now();
    long write_batches = 0;
    telemetry::obs::Context ctx;
    ctx.request_id = config_.request_id();
    ctx.dataset_id = run_id_;
    telemetry::obs::ScopedContext scope(ctx);
    telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "generation_start", "generator",
                             {{"request_id", config_.request_id()}, {"dataset_id", run_id_}});
    try {
        db_->CreateRun(run_id_, config_, "RUNNING", config_.request_id());
        
        InitializeHosts();
        
        auto start = ParseTime(config_.start_time_iso());
        auto end = ParseTime(config_.end_time_iso());
        auto duration = std::chrono::seconds(config_.interval_seconds());
        if (duration.count() == 0) duration = std::chrono::seconds(600); // default 10m
        
        long total_rows = 0;
        std::vector<TelemetryRecord> batch;
        const int BATCH_SIZE = 5000;
        
        for (auto t = start; t < end; t += duration) {
            db_->Heartbeat(IDbClient::JobType::Generation, run_id_);
            if (stop_flag_ && stop_flag_->load()) {
                spdlog::info("Generation run {} cancelled by request.", run_id_);
                db_->UpdateRunStatus(run_id_, "CANCELLED", total_rows);
                return;
            }
            for (const auto& host : hosts_) {
                batch.push_back(GenerateRecord(host, t));
                if (batch.size() >= BATCH_SIZE) {
                    db_->BatchInsertTelemetry(batch);
                    write_batches += 1;
                    total_rows += batch.size();
                    db_->UpdateRunStatus(run_id_, "RUNNING", total_rows);
                    batch.clear();
                    
                    if (stop_flag_ && stop_flag_->load()) {
                        spdlog::info("Generation run {} cancelled by request.", run_id_);
                        db_->UpdateRunStatus(run_id_, "CANCELLED", total_rows);
                        return;
                    }
                }
            }
        }
        
        // Final batch
        if (!batch.empty()) {
            db_->BatchInsertTelemetry(batch);
            write_batches += 1;
            total_rows += batch.size();
        }
        
        spdlog::info("Generation run {} complete. Total rows: {}", run_id_, total_rows);
        db_->UpdateRunStatus(run_id_, "SUCCEEDED", total_rows);
        auto end_time = std::chrono::steady_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        telemetry::obs::EmitHistogram("generation_duration_ms", duration_ms, "ms", "generator",
                                      {{"dataset_id", run_id_}});
        telemetry::obs::EmitCounter("generation_rows_written", total_rows, "rows", "generator",
                                    {{"dataset_id", run_id_}});
        telemetry::obs::EmitCounter("generation_db_write_count", write_batches, "batches", "generator",
                                    {{"dataset_id", run_id_}});
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "generation_end", "generator",
                                 {{"request_id", config_.request_id()},
                                  {"dataset_id", run_id_},
                                  {"rows", total_rows},
                                  {"duration_ms", duration_ms}});
        
    } catch (const std::exception& e) {
        spdlog::error("Generation run {} failed: {}", run_id_, e.what());
        db_->UpdateRunStatus(run_id_, "FAILED", 0, e.what());
        auto end_time = std::chrono::steady_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        telemetry::obs::EmitHistogram("generation_duration_ms", duration_ms, "ms", "generator",
                                      {{"dataset_id", run_id_}});
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "generation_error", "generator",
                                 {{"request_id", config_.request_id()},
                                  {"dataset_id", run_id_},
                                  {"error_code", telemetry::obs::kErrInternal},
                                  {"error", e.what()},
                                  {"duration_ms", duration_ms}});
    }
}
