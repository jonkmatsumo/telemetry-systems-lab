#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <iostream>

namespace telemetry::metrics {

class MetricsRegistry {
public:
    static MetricsRegistry& Instance() {
        static MetricsRegistry instance;
        return instance;
    }

    // Counters with labels
    void Increment(const std::string& name, const std::map<std::string, std::string>& labels, long value = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = SerializeKey(name, labels);
        counters_[key] += value;
    }

    // Gauges (for queue depth etc)
    void SetGauge(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        gauges_[name] = value;
    }

    // Histograms
    void RecordLatency(const std::string& name, const std::map<std::string, std::string>& labels, double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = SerializeKey(name, labels);
        auto& h = histograms_[key];
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

    // To Prometheus text format
    std::string ToPrometheus() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string out;
        for (const auto& kv : counters_) {
            out += kv.first + " " + std::to_string(kv.second) + "\n";
        }
        for (const auto& kv : gauges_) {
            out += kv.first + " " + std::to_string(kv.second) + "\n";
        }
        for (const auto& kv : histograms_) {
            out += kv.first + "_count " + std::to_string(kv.second.count) + "\n";
            out += kv.first + "_sum " + std::to_string(kv.second.sum) + "\n";
        }
        return out;
    }

private:
    MetricsRegistry() = default;
    std::mutex mutex_;
    std::map<std::string, long> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, HistogramStats> histograms_;

    std::string SerializeKey(const std::string& name, const std::map<std::string, std::string>& labels) {
        if (labels.empty()) return name;
        std::string key = name + "{";
        bool first = true;
        for (const auto& lp : labels) {
            if (!first) key += ",";
            key += lp.first + "=\"" + lp.second + "\"";
            first = false;
        }
        key += "}";
        return key;
    }
};

} // namespace telemetry::metrics
