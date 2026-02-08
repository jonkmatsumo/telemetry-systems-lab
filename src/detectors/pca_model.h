#pragma once

#include <string>
#include <vector>
#include "linalg/matrix.h"
#include "../contract.h"

namespace telemetry::anomaly {

struct PcaScore {
    bool is_anomaly = false;
    double reconstruction_error = 0.0;
    std::vector<double> residuals; // per-feature residual for explainability
    std::string details;
};

class PcaModel {
public:
    PcaModel() = default;

    // Load from model.json
    void Load(const std::string& artifact_path);

    // Score a vector
    [[nodiscard]] auto Score(const FeatureVector& vec) const -> PcaScore;

    // Accessors for testing
    [[nodiscard]] auto GetThreshold() const -> double { return threshold_; }
    [[nodiscard]] auto IsLoaded() const -> bool { return loaded_; }

    /**
     * @brief Estimates the memory footprint of the model in bytes.
     */
    [[nodiscard]] auto EstimateMemoryUsage() const -> size_t;

private:
    bool loaded_ = false;
    
    // Preprocessing (StandardScaler)
    linalg::Vector cur_mean_;
    linalg::Vector cur_scale_;

    // PCA
    // Components matrix (k x d)
    linalg::Matrix components_;
    // PCA Mean (centered) - often ~0 if StandardScaler checks out, but we track it for parity
    linalg::Vector pca_mean_;

    double threshold_ = 0.0;
};

} // namespace telemetry::anomaly
