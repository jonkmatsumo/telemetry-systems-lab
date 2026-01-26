#pragma once

#include <string>
#include <vector>

#include "linalg/matrix.h"

namespace telemetry {
namespace training {

struct PcaArtifact {
    linalg::Vector scaler_mean;
    linalg::Vector scaler_scale;
    linalg::Matrix components; // (k x d)
    linalg::Vector explained_variance;
    linalg::Vector pca_mean;
    double threshold = 0.0;
    int n_components = 0;
};

PcaArtifact TrainPcaFromDb(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           int n_components,
                           double percentile);

PcaArtifact TrainPcaFromSamples(const std::vector<linalg::Vector>& samples,
                                int n_components,
                                double percentile);

void WriteArtifactJson(const PcaArtifact& artifact,
                       const std::string& output_path);

} // namespace training
} // namespace telemetry
