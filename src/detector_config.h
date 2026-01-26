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
    double max_skip_window = 5; // how many samples can we skip updates for if they are anomalies
};

struct DetectorConfig {
    PreprocessingConfig preprocessing;
    WindowConfig window;
    OutlierConfig outliers;
};

} // namespace anomaly
} // namespace telemetry
