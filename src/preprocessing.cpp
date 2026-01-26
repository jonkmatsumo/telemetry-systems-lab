#include "preprocessing.h"

namespace telemetry {
namespace anomaly {

void Preprocessor::Apply(FeatureVector& vec) const {
    // 1. Clamp all values to be non-negative (sanity check)
    for (size_t i = 0; i < FeatureVector::kSize; ++i) {
        if (vec.data[i] < 0.0) {
            vec.data[i] = 0.0;
        }
    }

    // 2. Specific clamping for logic-bound metrics (percentages)
    // CPU, Mem, Disk are 0-100 theoretically, but we just ensure >= 0 above.
    // If we want to strictly clamp to 100, we can, but contract says "non-negative" mainly.
    // Let's stick to the requested "Clamp network rates to non-negative" from the prompt.
    // Actually, CPU/Mem/Disk also shouldn't be negative.
    
    // 3. Optional log1p for network
    if (config_.log1p_network) {
        vec.network_rx_rate() = std::log1p(vec.network_rx_rate());
        vec.network_tx_rate() = std::log1p(vec.network_tx_rate());
    }
}

} // namespace anomaly
} // namespace telemetry
