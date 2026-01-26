#include "detector_a.h"
#include <sstream>
#include <iomanip>

namespace telemetry {
namespace anomaly {

DetectorA::DetectorA(const WindowConfig& win_config, const OutlierConfig& outlier_config)
    : win_config_(win_config), outlier_config_(outlier_config) {
}

void DetectorA::UpdateRobustStats(MetricState& state) {
    if (state.buffer.empty()) return;

    // Copy to sort
    std::vector<double> data(state.buffer.begin(), state.buffer.end());
    size_t n = data.size();
    size_t mid = n / 2;
    
    // 1. Median
    std::nth_element(data.begin(), data.begin() + mid, data.end());
    state.median = data[mid];

    // 2. MAD
    std::vector<double> abs_diffs;
    abs_diffs.reserve(n);
    for (double val : data) {
        abs_diffs.push_back(std::abs(val - state.median));
    }
    std::nth_element(abs_diffs.begin(), abs_diffs.begin() + mid, abs_diffs.end());
    state.mad = abs_diffs[mid];
    
    // Avoid division by zero later
    if (state.mad == 0.0) state.mad = 1e-6; 
}

AnomalyScore DetectorA::Update(const FeatureVector& vec) {
    AnomalyScore score;
    std::stringstream ss;
    bool flagged = false;

    // Check robust recompute tick
    bool needs_recompute = (update_count_ % win_config_.recompute_interval == 0);

    for (size_t i = 0; i < FeatureVector::kSize; ++i) {
        auto& state = states_[i];
        double val = vec.data[i];

        // 1. Update Window
        state.buffer.push_back(val);
        state.sum += val;
        state.sum_sq += val * val;

        if (state.buffer.size() > (size_t)win_config_.size) {
            double old = state.buffer.front();
            state.buffer.pop_front();
            state.sum -= old;
            state.sum_sq -= old * old;
        }

        // 2. Recompute Robust (if scheduled)
        if (needs_recompute && state.buffer.size() >= (size_t)win_config_.min_history) {
             UpdateRobustStats(state);
        }

        // 3. Score (only if warmed up)
        if (state.buffer.size() >= (size_t)win_config_.min_history) {
            // Robust Z-score: 0.6745 * (x - median) / MAD
            // We'll use simple: (x - median) / MAD
            // Standard Z-score: (x - mean) / std
            
            double robust_z = 0.0;
            if (state.mad > 0) {
                 robust_z = std::abs(val - state.median) / state.mad;
            }

            if (robust_z > outlier_config_.robust_z_threshold) {
                flagged = true;
                if (robust_z > score.max_z_score) score.max_z_score = robust_z;
                
                ss << FeatureMetadata::GetFeatureNames()[i] 
                   << ":rz=" << std::fixed << std::setprecision(1) << robust_z << " ";
            }
        }
    }

    update_count_++;
    
    if (flagged) {
        score.is_anomaly = true;
        score.details = ss.str();
    }

    return score;
}

} // namespace anomaly
} // namespace telemetry
