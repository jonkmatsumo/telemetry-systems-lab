#include "alert_manager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace telemetry {
namespace anomaly {

using json = nlohmann::json;

std::vector<Alert> AlertManager::Evaluate(const std::string& host_id, 
                            const std::string& run_id,
                            std::chrono::system_clock::time_point ts,
                            bool detector_a_flag, double scores_a,
                            bool detector_b_flag, double scores_b,
                            const std::string& details) 
{
    std::vector<Alert> alerts;
    auto& state = states_[host_id];

    bool any_flag = detector_a_flag || detector_b_flag;

    if (any_flag) {
        state.consecutive_anomalies++;
    } else {
        state.consecutive_anomalies = 0;
        return alerts; // No alert
    }

    // Check Hysteresis
    if (state.consecutive_anomalies < hysteresis_threshold_) {
        return alerts; // Waiting for more confirmation
    }

    // Check Cooldown
    if (state.last_alert_time.time_since_epoch().count() > 0) {
        if ((ts - state.last_alert_time) < cooldown_s_) {
            return alerts; // In cooldown
        }
    }

    // Generate Alert
    Alert alert;
    alert.host_id = host_id;
    alert.run_id = run_id;
    alert.timestamp = ts;
    alert.details_json = details;
    
    // Determine Severity
    // Fusion Logic: 
    // If Both -> CRITICAL
    // If B (PCA) -> HIGH (structural change)
    // If A (Stats) -> MEDIUM (outlier)
    // If A and Score > 10.0 -> HIGH
    
    // Safety check: ensure we don't treat non-evaluated B as a valid score if flag was somehow true (unlikely)
    // But mainly to clarify logic.
    
    if (detector_a_flag && detector_b_flag) {
        alert.severity = "CRITICAL";
        alert.source = "FUSION_A_B";
        alert.score = std::max(scores_a, scores_b);
    } else if (detector_b_flag) {
        alert.severity = "HIGH";
        alert.source = "DETECTOR_B_PCA";
        alert.score = scores_b;
    } else {
        alert.severity = (scores_a > 10.0) ? "HIGH" : "MEDIUM";
        alert.source = "DETECTOR_A_STATS";
        alert.score = scores_a;
    }

    alerts.push_back(alert);
    
    // Update State
    state.last_alert_time = ts;
    // We might reset consecutive anomalies or keep counting?
    // Let's reset to enforce hysteresis again? 
    // Or just cooldown handles the rate limiting.
    // Resetting hysteresis allows "flapping" to be caught again after cooldown if persistent.
    state.consecutive_anomalies = 0; 
    
    return alerts;
}

} // namespace anomaly
} // namespace telemetry
