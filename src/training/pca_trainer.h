#pragma once

#include <string>
#include <vector>
#include <optional>

#include "linalg/matrix.h"

#include "db_connection_manager.h"

namespace telemetry::training {

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

constexpr const char* kHpoGeneratorVersion = "hpo_generator_v1";

enum class HpoCapReason {
    NONE,
    MAX_TRIALS,
    GRID_CAP
};

struct HpoPreflight {
    int estimated_candidates = 0;
    int effective_trials = 0;
    HpoCapReason capped_by = HpoCapReason::NONE;
};

auto PreflightHpoConfig(const HpoConfig& hpo) -> HpoPreflight;

auto ValidateHpoConfig(const HpoConfig& config) -> std::vector<HpoValidationError>;

auto GenerateTrials(const HpoConfig& hpo, const std::string& dataset_id) -> std::vector<TrainingConfig>;

auto ComputeCandidateFingerprint(const HpoConfig& hpo) -> std::string;

struct PcaArtifact {
    linalg::Vector scaler_mean;
    linalg::Vector scaler_scale;
    linalg::Matrix components; // (k x d)
    linalg::Vector explained_variance;
    linalg::Vector pca_mean;
    double threshold = 0.0;
    int n_components = 0;
};

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
auto TrainPcaFromDb(std::shared_ptr<DbConnectionManager> manager,
                           const std::string& dataset_id,
                           int n_components,
                           double percentile,
                           std::function<void()> heartbeat = nullptr) -> PcaArtifact;

auto TrainPcaFromDbBatched(std::shared_ptr<DbConnectionManager> manager,
                                  const std::string& dataset_id,
                                  int n_components,
                                  double percentile,
                                  size_t batch_size,
                                  std::function<void()> heartbeat = nullptr) -> PcaArtifact;

auto TrainPcaFromSamples(const std::vector<linalg::Vector>& samples,
                                int n_components,
                                double percentile) -> PcaArtifact;
// NOLINTEND(bugprone-easily-swappable-parameters)

void WriteArtifactJson(const PcaArtifact& artifact,
                       const std::string& output_path);

} // namespace telemetry::training
