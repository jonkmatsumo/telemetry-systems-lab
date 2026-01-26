#include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include "detectors/pca_model.h"

using namespace telemetry::anomaly;
using json = nlohmann::json;

class ParityBTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Assume running from build directory or repo root. 
         // We might need to adjust paths.
    }
};

TEST_F(ParityBTest, MatchesGoldenRegression) {
    PcaModel model;
    // Load artifact (path relative to repo root assumed)
    // In CTest, working dir is usually build dir, so we need typical path handling
    std::string model_path = "../artifacts/pca/default/model.json";
    if (std::ifstream(model_path).fail()) {
        model_path = "artifacts/pca/default/model.json";
    }
    ASSERT_NO_THROW(model.Load(model_path));

    // Load golden regression data (C++-canonical artifacts)
    std::string golden_path = "../tests/parity/golden/parity_b.json";
    if (std::ifstream(golden_path).fail()) {
        golden_path = "tests/parity/golden/parity_b.json";
    }
    
    std::ifstream f(golden_path);
    ASSERT_TRUE(f.is_open()) << "Could not open golden data: " << golden_path;
    json j;
    f >> j;
    
    for (const auto& sample : j["samples"]) {
        std::vector<double> input = sample["input"].get<std::vector<double>>();
        double expected_err = sample["expected_error"].get<double>();
        bool expected_anomaly = sample["is_anomaly"].get<bool>();
        
        FeatureVector v;
        for(size_t i=0; i<5; ++i) v.data[i] = input[i];
        
        auto score = model.Score(v);
        
        // Strict tolerance
        EXPECT_NEAR(score.reconstruction_error, expected_err, 1e-5) 
            << "Mismatch for input: " << input[0] << ", ...";
        EXPECT_EQ(score.is_anomaly, expected_anomaly);
    }
}
