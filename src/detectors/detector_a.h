#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <string>

#include "../contract.h"
#include "../detector_config.h"

namespace telemetry::anomaly {

struct AnomalyScore {
    bool is_anomaly = false;
    double max_z_score = 0.0;
    std::string details; // JSON or text description
};

class DetectorA {
public:
    explicit DetectorA(const WindowConfig& win_config, const OutlierConfig& outlier_config);

    // Update state with new vector and return score
    auto Update(const FeatureVector& vec) -> AnomalyScore;

private:
    struct MetricState {
        std::deque<double> buffer; // efficient enough for small W (~60-300)
        double sum = 0.0;
        double sum_sq = 0.0;
        
        // Robust stats
        double median = 0.0;
        double mad = 0.0; // Median Absolute Deviation
        
        // Compute Mean/Std from sum/sum_sq
        auto Mean(size_t n) const -> double { return n > 0 ? sum / static_cast<double>(n) : 0.0; }
        auto Std(size_t n) const -> double {
            if (n < 2) { return 0.0; }
            double mean = Mean(n);
            double variance = (sum_sq / static_cast<double>(n)) - (mean * mean);
            return variance > 0 ? std::sqrt(variance) : 0.0;
        }
    };

    auto UpdateRobustStats(MetricState& state) -> void;

    WindowConfig win_config_;
    OutlierConfig outlier_config_;
    
    std::array<MetricState, FeatureVector::kSize> states_;
    long update_count_ = 0;
};

} // namespace telemetry::anomaly