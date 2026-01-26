#pragma once

#include <string>
#include <vector>
#include "preprocessing.h"

namespace telemetry {
namespace anomaly {

struct WindowConfig {
    int size = 60; // default window size (e.g. 60 samples)
    int recompute_interval = 10; // how often to recompute robust stats
    int min_history = 10; // warmup period
};

struct OutlierConfig {
    double z_score_threshold = 3.0;
    double robust_z_threshold = 3.5;
    
    // Poisoning Mitigation
    bool enable_poison_mitigation = false;
    double poison_skip_threshold = 7.0; // Higher than detection threshold to only skip obvious outliers
};

struct GatingConfig {
    bool enable_gating = false;
    double z_trigger_threshold = 3.0;
    long period_ms = 60000;
};

struct DetectorConfig {
    PreprocessingConfig preprocessing;
    WindowConfig window;
    OutlierConfig outliers;
    GatingConfig gating;
};

} // namespace anomaly
} // namespace telemetry
