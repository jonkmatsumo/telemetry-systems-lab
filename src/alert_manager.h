#pragma once

#include <vector>
#include <string>
#include <map>
#include <chrono>
#include "contract.h"
#include "detector_config.h"
#include "types.h"

namespace telemetry {
namespace anomaly {

struct FusionState {
    int consecutive_anomalies = 0;
    std::chrono::system_clock::time_point last_alert_time;
};

class AlertManager {
public:
    explicit AlertManager(int hysteresis_threshold = 2, int cooldown_seconds = 600)
        : hysteresis_threshold_(hysteresis_threshold), cooldown_s_(cooldown_seconds) {}

    // Evaluate fusion logic
    // Returns empty optional (std::vector empty) if no alert
    std::vector<Alert> Evaluate(const std::string& host_id, 
                                const std::string& run_id,
                                std::chrono::system_clock::time_point ts,
                                bool detector_a_flag, double scores_a,
                                bool detector_b_flag, double scores_b,
                                const std::string& details);

private:
    int hysteresis_threshold_;
    std::chrono::seconds cooldown_s_;
    std::map<std::string, FusionState> states_;
};

} // namespace anomaly
} // namespace telemetry
