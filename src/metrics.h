#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <iostream>
#include <set>

namespace telemetry::metrics {

class MetricsRegistry {
public:
    static auto Instance() -> MetricsRegistry& {
        static MetricsRegistry instance;
        return instance;
    }

    // Counters with labels
    void Increment(const std::string& name, const std::map<std::string, std::string>& labels, long value = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = SerializeKey(name, labels);
        if (!ShouldTrackSeries(name, key)) {
            return;
        }
        counters_[key] += value;
    }

    // Gauges (for queue depth etc)
    void SetGauge(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ShouldTrackSeries(name, name)) {
            return;
        }
        gauges_[name] = value;
    }

    // Histograms
    void RecordLatency(const std::string& name, const std::map<std::string, std::string>& labels, double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = SerializeKey(name, labels);
        if (!ShouldTrackSeries(name, key)) {
            return;
        }
        auto& h = histograms_[key];
        h.count++;
        h.sum += ms;
        if (ms < h.min) { h.min = ms; }
        if (ms > h.max) { h.max = ms; }
    }

    struct HistogramStats {
        long count = 0;
        double sum = 0.0;
        double min = 1e9;
        double max = 0.0;
    };

    // To Prometheus text format
    auto ToPrometheus() -> std::string {
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
        if (dropped_series_total_ > 0) {
            out += "metrics_series_dropped_total " + std::to_string(dropped_series_total_) + "\n";
        }
        return out;
    }

    void ConfigureSeriesCardinalityGuard(size_t series_cap,
                                         const std::set<std::string>& allowlist = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        series_cap_ = series_cap;
        series_allowlist_ = allowlist;
        series_allowlist_.insert("metrics_series_dropped_total");
    }

    void ResetForTesting() {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
        seen_series_.clear();
        dropped_series_total_ = 0;
        warned_series_.clear();
        series_cap_ = kDefaultSeriesCap;
        series_allowlist_.clear();
        series_allowlist_.insert("metrics_series_dropped_total");
    }

private:
    static constexpr size_t kDefaultSeriesCap = 2000;
    MetricsRegistry() = default;
    std::mutex mutex_;
    std::map<std::string, long> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, HistogramStats> histograms_;
    size_t series_cap_ = kDefaultSeriesCap;
    std::set<std::string> series_allowlist_ = {"metrics_series_dropped_total"};
    std::set<std::string> seen_series_;
    long dropped_series_total_ = 0;
    std::set<std::string> warned_series_;

    auto ShouldTrackSeries(const std::string& name, const std::string& key) -> bool {
        if (series_allowlist_.count(name) > 0) {
            return true;
        }
        if (seen_series_.count(key) > 0) {
            return true;
        }
        if (seen_series_.size() < series_cap_) {
            seen_series_.insert(key);
            return true;
        }

        dropped_series_total_++;
        if (warned_series_.insert(key).second) {
            std::cerr << "[warn] metrics series cap reached (" << series_cap_
                      << "); dropping new series: " << key << std::endl;
        }
        return false;
    }

    auto SerializeKey(const std::string& name, const std::map<std::string, std::string>& labels) -> std::string {
        if (labels.empty()) { return name; }
        std::string key = name + "{";
        bool first = true;
        for (const auto& lp : labels) {
            if (!first) { key += ","; }
            key += lp.first + "=\"" + lp.second + "\"";
            first = false;
        }
        key += "}";
        return key;
    }
};

} // namespace telemetry::metrics
