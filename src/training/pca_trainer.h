#pragma once

#include <string>
#include <vector>

#include "linalg/matrix.h"

namespace telemetry {
namespace training {

struct TrainingConfig {
    std::string dataset_id;
    int n_components = 3;
    double percentile = 99.5;
    std::vector<std::string> feature_set = {"cpu_usage", "memory_usage", "disk_utilization", "network_rx_rate", "network_tx_rate"};
};

struct SearchSpace {
    std::vector<int> n_components;
    std::vector<double> percentile;
};

struct HpoConfig {
    std::string algorithm = "grid"; // "grid" or "random"
    int max_trials = 10;
    int max_concurrency = 2;
    std::optional<int> seed;
    SearchSpace search_space;
};

struct HpoValidationError {
    std::string field;
    std::string message;
};

std::vector<HpoValidationError> ValidateHpoConfig(const HpoConfig& config);

std::vector<TrainingConfig> GenerateTrials(const HpoConfig& hpo, const std::string& dataset_id);

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

PcaArtifact TrainPcaFromDbBatched(const std::string& db_conn_str,
                                  const std::string& dataset_id,
                                  int n_components,
                                  double percentile,
                                  size_t batch_size);

PcaArtifact TrainPcaFromSamples(const std::vector<linalg::Vector>& samples,
                                int n_components,
                                double percentile);

void WriteArtifactJson(const PcaArtifact& artifact,
                       const std::string& output_path);

} // namespace training
} // namespace telemetry
