#include <filesystem>
#include <gtest/gtest.h>

#include "detectors/pca_model.h"
#include "training/pca_trainer.h"

TEST(PcaTrainerTest, TrainsOnSyntheticData) {
    std::vector<telemetry::linalg::Vector> samples;
    samples.reserve(200);

    for (int i = 0; i < 200; ++i) {
        double base = static_cast<double>(i % 50);
        telemetry::linalg::Vector x(5, 0.0);
        x[0] = 40.0 + base;
        x[1] = 0.8 * x[0] + 5.0;
        x[2] = 30.0 + (i % 3);
        x[3] = 50.0 + base * 0.5;
        x[4] = 0.5 * x[3] + 2.0;
        samples.push_back(x);
    }

    auto artifact = telemetry::training::TrainPcaFromSamples(samples, 3, 99.5);

    EXPECT_EQ(artifact.n_components, 3);
    EXPECT_EQ(artifact.scaler_mean.size(), 5u);
    EXPECT_EQ(artifact.scaler_scale.size(), 5u);
    EXPECT_EQ(artifact.components.rows, 3u);
    EXPECT_EQ(artifact.components.cols, 5u);
    EXPECT_EQ(artifact.explained_variance.size(), 3u);
    EXPECT_EQ(artifact.pca_mean.size(), 5u);
    EXPECT_GE(artifact.threshold, 0.0);
}

TEST(PcaTrainerTest, ArtifactLoadsInPcaModel) {
    std::vector<telemetry::linalg::Vector> samples;
    samples.reserve(50);
    for (int i = 0; i < 50; ++i) {
        telemetry::linalg::Vector x(5, 0.0);
        x[0] = 10.0 + i;
        x[1] = 20.0 + i * 0.5;
        x[2] = 30.0 + (i % 5);
        x[3] = 40.0 + i * 0.2;
        x[4] = 50.0 + i * 0.1;
        samples.push_back(x);
    }

    auto artifact = telemetry::training::TrainPcaFromSamples(samples, 3, 99.5);

    const std::string path = "tests/parity/golden/test_pca_model.json";
    std::filesystem::create_directories("tests/parity/golden");
    telemetry::training::WriteArtifactJson(artifact, path);

    telemetry::anomaly::PcaModel model;
    ASSERT_NO_THROW(model.Load(path));
}

TEST(PcaTrainerTest, ValidatesNComponents) {
    std::vector<telemetry::linalg::Vector> samples = {telemetry::linalg::Vector(5, 1.0), telemetry::linalg::Vector(5, 2.0)};
    
    // n_components = 0
    EXPECT_THROW(telemetry::training::TrainPcaFromSamples(samples, 0, 99.5), std::invalid_argument);
    
    // n_components = 6 (more than 5 features)
    EXPECT_THROW(telemetry::training::TrainPcaFromSamples(samples, 6, 99.5), std::invalid_argument);
    
    // n_components = -1
    EXPECT_THROW(telemetry::training::TrainPcaFromSamples(samples, -1, 99.5), std::invalid_argument);
}

TEST(HpoContractTest, ValidatesInvalidAlgorithm) {
    telemetry::training::HpoConfig config;
    config.algorithm = "unsupported";
    auto errors = telemetry::training::ValidateHpoConfig(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].field, "algorithm");
}

TEST(HpoContractTest, ValidatesEmptySearchSpace) {
    telemetry::training::HpoConfig config;
    config.search_space.n_components = {};
    config.search_space.percentile = {};
    auto errors = telemetry::training::ValidateHpoConfig(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].field, "search_space");
}

TEST(HpoContractTest, ValidatesGridSearchCap) {
    telemetry::training::HpoConfig config;
    config.algorithm = "grid";
    // Valid n_components (1-5), but many of them
    config.search_space.n_components = {1, 2, 3, 4, 5}; 
    // Valid percentiles, but many of them
    config.search_space.percentile = {90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 99.1, 99.2, 99.3, 99.4, 99.5, 99.6, 99.7, 99.8, 99.9, 99.99, 99.999};
    // 5 * 21 = 105 combinations, cap is 100
    auto errors = telemetry::training::ValidateHpoConfig(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].field, "search_space");
    EXPECT_EQ(errors[0].message, "Grid search space too large (max 100 combinations)");
}

TEST(HpoContractTest, ValidatesMaxConcurrency) {
    telemetry::training::HpoConfig config;
    config.max_concurrency = 0;
    auto errors = telemetry::training::ValidateHpoConfig(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].field, "max_concurrency");

    config.max_concurrency = 11;
    errors = telemetry::training::ValidateHpoConfig(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].field, "max_concurrency");
}

TEST(HpoContractTest, GeneratesDeterministicGrid) {
    telemetry::training::HpoConfig config;
    config.algorithm = "grid";
    config.max_trials = 10;
    config.search_space.n_components = {2, 3};
    config.search_space.percentile = {99.0, 99.5};

    auto trials1 = telemetry::training::GenerateTrials(config, "ds1");
    auto trials2 = telemetry::training::GenerateTrials(config, "ds1");

    ASSERT_EQ(trials1.size(), 4u);
    ASSERT_EQ(trials1.size(), trials2.size());
    for (size_t i = 0; i < trials1.size(); ++i) {
        EXPECT_EQ(trials1[i].n_components, trials2[i].n_components);
        EXPECT_EQ(trials1[i].percentile, trials2[i].percentile);
    }
}

TEST(HpoContractTest, GeneratesDeterministicSeededRandom) {
    telemetry::training::HpoConfig config;
    config.algorithm = "random";
    config.max_trials = 5;
    config.seed = 42;
    config.search_space.n_components = {2, 3, 4, 5};
    config.search_space.percentile = {90.0, 95.0, 99.0, 99.9};

    auto trials1 = telemetry::training::GenerateTrials(config, "ds1");
    auto trials2 = telemetry::training::GenerateTrials(config, "ds1");

    ASSERT_EQ(trials1.size(), 5u);
    for (size_t i = 0; i < trials1.size(); ++i) {
        EXPECT_EQ(trials1[i].n_components, trials2[i].n_components);
        EXPECT_EQ(trials1[i].percentile, trials2[i].percentile);
    }
}

TEST(HpoContractTest, PreflightCalculatesCorrectly) {
    telemetry::training::HpoConfig config;
    config.algorithm = "grid";
    config.search_space.n_components = {2, 3, 4};
    config.search_space.percentile = {99.0, 99.5};
    config.max_trials = 10;

    auto preflight = telemetry::training::PreflightHpoConfig(config);
    EXPECT_EQ(preflight.estimated_candidates, 6);
    EXPECT_EQ(preflight.effective_trials, 6);
    EXPECT_EQ(preflight.capped_by, telemetry::training::HpoCapReason::NONE);

    config.max_trials = 4;
    preflight = telemetry::training::PreflightHpoConfig(config);
    EXPECT_EQ(preflight.effective_trials, 4);
    EXPECT_EQ(preflight.capped_by, telemetry::training::HpoCapReason::MAX_TRIALS);

    // Test GRID_CAP (100)
    config.search_space.n_components = {1, 2, 3, 4, 5};
    config.search_space.percentile = {};
    for (int i = 0; i < 30; ++i) config.search_space.percentile.push_back(90.0 + i * 0.1);
    // 5 * 30 = 150 combinations
    preflight = telemetry::training::PreflightHpoConfig(config);
    EXPECT_EQ(preflight.estimated_candidates, 150);
    EXPECT_EQ(preflight.effective_trials, 100);
    EXPECT_EQ(preflight.capped_by, telemetry::training::HpoCapReason::GRID_CAP);
}

TEST(HpoContractTest, FingerprintIsStable) {
    telemetry::training::HpoConfig config;
    config.algorithm = "random";
    config.max_trials = 10;
    config.seed = 123;
    config.search_space.n_components = {2, 4};
    config.search_space.percentile = {99.0};

    std::string fp1 = telemetry::training::ComputeCandidateFingerprint(config);
    std::string fp2 = telemetry::training::ComputeCandidateFingerprint(config);
    EXPECT_EQ(fp1, fp2);

    // Permute search space - should still be same fingerprint because we sort it
    config.search_space.n_components = {4, 2};
    std::string fp3 = telemetry::training::ComputeCandidateFingerprint(config);
    EXPECT_EQ(fp1, fp3);

    // Change something - should be different
    config.max_trials = 11;
    std::string fp4 = telemetry::training::ComputeCandidateFingerprint(config);
    EXPECT_NE(fp1, fp4);
}
