#include <gtest/gtest.h>
#include "pca_model_cache.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

using namespace telemetry::anomaly;

// Helper to create a dummy model artifact
void CreateDummyModel(const std::string& path) {
    nlohmann::json j;
    j["preprocessing"]["mean"] = {0.0, 0.0, 0.0, 0.0, 0.0};
    j["preprocessing"]["scale"] = {1.0, 1.0, 1.0, 1.0, 1.0};
    j["model"]["components"] = {
        {1.0, 0.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0, 0.0}
    };
    j["model"]["mean"] = {0.0, 0.0, 0.0, 0.0, 0.0};
    j["thresholds"]["reconstruction_error"] = 10.0;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    f << j.dump();
}

TEST(PcaModelCacheTest, HitMissLogic) {
    std::string path = "tmp/test_cache_model.json";
    CreateDummyModel(path);

    PcaModelCache cache(2, 1024 * 1024, 60);
    EXPECT_EQ(cache.GetStats().misses, 0);
    EXPECT_EQ(cache.GetStats().hits, 0);

    // Miss 1
    auto m1 = cache.GetOrCreate("model1", path);
    EXPECT_NE(m1, nullptr);
    EXPECT_EQ(cache.GetStats().misses, 1);
    EXPECT_EQ(cache.GetStats().hits, 0);

    // Hit 1
    auto m1_hit = cache.GetOrCreate("model1", path);
    EXPECT_EQ(m1, m1_hit);
    EXPECT_EQ(cache.GetStats().misses, 1);
    EXPECT_EQ(cache.GetStats().hits, 1);

    // Miss 2
    auto m2 = cache.GetOrCreate("model2", path);
    EXPECT_EQ(cache.GetStats().misses, 2);
    EXPECT_EQ(cache.GetStats().hits, 1);

    std::filesystem::remove_all("tmp");
}

TEST(PcaModelCacheTest, EvictionLogic) {
    std::string path = "tmp/test_cache_model_evict.json";
    CreateDummyModel(path);

    PcaModelCache cache(2, 1024 * 1024, 60); // Max 2 entries

    cache.GetOrCreate("m1", path);
    cache.GetOrCreate("m2", path);
    EXPECT_EQ(cache.GetStats().size, 2);

    // Access m1 to make it newer than m2
    cache.GetOrCreate("m1", path);

    // Add m3, should evict m2 (LRU)
    cache.GetOrCreate("m3", path);
    EXPECT_EQ(cache.GetStats().size, 2);
    EXPECT_EQ(cache.GetStats().evictions, 1);

    // Verify m2 is gone
    EXPECT_EQ(cache.GetStats().size, 2);
    
    std::filesystem::remove_all("tmp");
}

TEST(PcaModelCacheTest, ByteLimitEviction) {
    std::string path = "tmp/test_cache_model_bytes.json";
    CreateDummyModel(path);

    // Get size of one model
    PcaModel temp;
    temp.Load(path);
    size_t model_size = temp.EstimateMemoryUsage();

    // Set cache limit to just enough for 1 model
    PcaModelCache cache(10, model_size + 10, 60);

    cache.GetOrCreate("m1", path);
    EXPECT_EQ(cache.GetStats().size, 1);
    EXPECT_GT(cache.GetStats().bytes_used, 0);

    // Add m2, should evict m1
    cache.GetOrCreate("m2", path);
    EXPECT_EQ(cache.GetStats().size, 1);
    EXPECT_EQ(cache.GetStats().evictions, 1);

    std::filesystem::remove_all("tmp");
}

TEST(PcaModelCacheTest, InvalidationAndTtl) {
    std::string path = "tmp/test_cache_model_ttl.json";
    CreateDummyModel(path);

    PcaModelCache cache(10, 1024 * 1024, 0); // 0 TTL means everything expires immediately

    cache.GetOrCreate("m1", path);
    // Next access should be a miss due to 0 TTL
    cache.GetOrCreate("m1", path);
    EXPECT_EQ(cache.GetStats().misses, 2);

    // Explicit invalidation
    PcaModelCache cache2(10, 1024 * 1024, 60);
    cache2.GetOrCreate("m1", path);
    cache2.Invalidate("m1");
    cache2.GetOrCreate("m1", path);
    EXPECT_EQ(cache2.GetStats().misses, 2);

    std::filesystem::remove_all("tmp");
}

} // namespace
