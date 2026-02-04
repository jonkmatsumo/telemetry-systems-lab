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
    config.search_space.n_components = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    config.search_space.percentile = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    // 11 * 10 = 110 combinations, cap is 100
    auto errors = telemetry::training::ValidateHpoConfig(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].field, "search_space");
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
