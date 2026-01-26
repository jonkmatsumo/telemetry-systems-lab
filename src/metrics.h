#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <iostream>

namespace telemetry {
namespace metrics {

class MetricsRegistry {
public:
    static MetricsRegistry& Instance() {
        static MetricsRegistry instance;
        return instance;
    }

    // Counters
    void Increment(const std::string& name, long value = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_[name] += value;
    }

    long GetCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        return counters_[name];
    }

    // Histograms (Simplified as sum/count for now to avoid buckets complexity in minimal MVP)
    // Or just store raw observations? Let's just track count/sum/min/max.
    void RecordLatency(const std::string& name, double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& h = histograms_[name];
        h.count++;
        h.sum += ms;
        if (ms < h.min) h.min = ms;
        if (ms > h.max) h.max = ms;
    }

    struct HistogramStats {
        long count = 0;
        double sum = 0.0;
        double min = 1e9;
        double max = 0.0;
    };

    HistogramStats GetHistogram(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        return histograms_[name];
    }

    // Dump to string (for logs)
    std::string Dump() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string out = "\n--- Metrics Dump ---\n";
        for (const auto& kv : counters_) {
            out += "COUNTER " + kv.first + ": " + std::to_string(kv.second) + "\n";
        }
        for (const auto& kv : histograms_) {
            double avg = kv.second.count > 0 ? kv.second.sum / kv.second.count : 0.0;
            out += "HISTOGRAM " + kv.first + ": count=" + std::to_string(kv.second.count) 
                + " avg=" + std::to_string(avg) + " max=" + std::to_string(kv.second.max) + "\n";
        }
        out += "--------------------\n";
        return out;
    }

private:
    MetricsRegistry() = default;
    std::mutex mutex_;
    std::map<std::string, long> counters_;
    std::map<std::string, HistogramStats> histograms_;
};

} // namespace metrics
} // namespace telemetry
