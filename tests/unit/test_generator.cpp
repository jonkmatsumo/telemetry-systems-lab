#include <gtest/gtest.h>
#include "generator.h"
#include "mocks/mock_db_client.h"
#include <memory>

// Expose protected members for testing
class TestGenerator : public Generator {
public:
    using Generator::Generator;
    using Generator::GenerateRecord;
    using Generator::InitializeHosts;
    using Generator::hosts_;
    
    // Helper to run logic without DB loop
    void Setup() { InitializeHosts(); }
    
    const std::vector<HostProfile>& GetHosts() const { return hosts_; }
};

TEST(GeneratorTest, HostInitialization) {
    telemetry::GenerateRequest req;
    req.set_tier("ALPHA");
    req.set_host_count(10);
    req.set_seed(12345);
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run", db);
    gen.Setup();
    
    const auto& hosts = gen.GetHosts();
    ASSERT_EQ(hosts.size(), 10);
    EXPECT_EQ(hosts[0].host_id, "host-ALPHA-0");
    EXPECT_EQ(hosts[9].host_id, "host-ALPHA-9");
}

TEST(GeneratorMathTest, BoundsCheck) {
    telemetry::GenerateRequest req;
    req.set_tier("BETA");
    req.set_seed(42);
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run-2", db);
    
    HostProfile h;
    h.host_id = "test-host";
    h.cpu_base = 50.0;
    h.mem_base = 60.0;
    h.phase_shift = 0.0;
    
    auto now = std::chrono::system_clock::now();
    auto rec = gen.GenerateRecord(h, now);
    
    EXPECT_GE(rec.cpu_usage, 0.0);
    EXPECT_LE(rec.cpu_usage, 100.0);
    EXPECT_GE(rec.memory_usage, 0.0);
    EXPECT_LE(rec.memory_usage, 100.0);
}

TEST(GeneratorMathTest, AnomalyTrigger) {
    telemetry::GenerateRequest req;
    req.set_tier("GAMMA");
    req.mutable_anomaly_config()->set_point_rate(1.0); // Force spike
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run-3", db);
    
    HostProfile h;
    h.host_id = "test-host";
    h.cpu_base = 10.0;
    
    auto now = std::chrono::system_clock::now();
    auto rec = gen.GenerateRecord(h, now);
    
    EXPECT_TRUE(rec.is_anomaly);
    EXPECT_NE(rec.anomaly_type.find("POINT_SPIKE"), std::string::npos);
    EXPECT_GE(rec.cpu_usage, 50.0); 
}
