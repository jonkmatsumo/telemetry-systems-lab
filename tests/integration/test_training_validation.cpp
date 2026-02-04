#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cstdlib>

class TrainingValidationTest : public ::testing::Test {
protected:
    std::string api_url;
    std::unique_ptr<httplib::Client> client;

    void SetUp() override {
        const char* env_url = std::getenv("API_URL");
        if (env_url) {
            api_url = env_url;
        } else {
            api_url = "http://localhost:8280";
        }
        client = std::make_unique<httplib::Client>(api_url.c_str());
    }
};

TEST_F(TrainingValidationTest, RejectsInvalidComponents) {
    nlohmann::json body = {
        {"dataset_id", "00000000-0000-0000-0000-000000000000"},
        {"n_components", 10} // Too high, max 5
    };
    auto res = client->Post("/train", body.dump(), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    auto j = nlohmann::json::parse(res->body);
    EXPECT_TRUE(j.contains("error"));
}

TEST_F(TrainingValidationTest, RejectsInvalidPercentile) {
    nlohmann::json body = {
        {"dataset_id", "00000000-0000-0000-0000-000000000000"},
        {"percentile", 10.0} // Too low, min 50.0
    };
    auto res = client->Post("/train", body.dump(), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(TrainingValidationTest, AcceptsValidParameters) {
    // We don't necessarily need a real dataset if we just want to test validation 
    // before it hits the DB (if we validated before DB check).
    // In our implementation, we validate BEFORE CreateModelRun.
    
    nlohmann::json body = {
        {"dataset_id", "00000000-0000-0000-0000-000000000000"},
        {"n_components", 3},
        {"percentile", 99.0}
    };
    auto res = client->Post("/train", body.dump(), "application/json");
    ASSERT_TRUE(res);
    // It might still fail with 404 or 500 if dataset not found, but we check if it PASSED validation.
    // Actually, HandleTrainModel calls CreateModelRun which might fail if dataset_id FK fails.
    // But we expect it NOT to be 400.
    EXPECT_NE(res->status, 400);
}
