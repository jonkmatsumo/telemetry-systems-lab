#pragma once
#include <string>
#include <vector>
#include <chrono>

struct HostProfile {
    std::string host_id;
    std::string project_id;
    std::string region;
    std::string labels_json;
    
    // Baseline params
    double cpu_base;
    double mem_base;
    double phase_shift; // 0 to 2pi
    
    // Anomaly State
    int burst_remaining = 0;
    bool correlation_broken = false;
    int correlation_break_remaining = 0;
};


struct TelemetryRecord {
    std::chrono::system_clock::time_point metric_timestamp;
    std::chrono::system_clock::time_point ingestion_time;
    
    std::string host_id;
    std::string project_id;
    std::string region;
    
    double cpu_usage;
    double memory_usage;
    double disk_utilization;
    double network_rx_rate; // MB/s
    double network_tx_rate; // MB/s
    
    std::string labels_json;
    std::string run_id;
    
    bool is_anomaly = false;
    std::string anomaly_type;
};
