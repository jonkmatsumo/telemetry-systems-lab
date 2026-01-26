#pragma once

#include "contract.h"
#include <cmath>
#include <algorithm>

namespace telemetry {
namespace anomaly {

struct PreprocessingConfig {
    bool log1p_network = false;
    // Potentially other clamps or normalization flags here
};

class Preprocessor {
public:
    explicit Preprocessor(PreprocessingConfig config) : config_(config) {}

    // In-place modification of the vector
    void Apply(FeatureVector& vec) const;

private:
    PreprocessingConfig config_;
};

} // namespace anomaly
} // namespace telemetry
