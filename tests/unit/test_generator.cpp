#include <gtest/gtest.h>
#include "generator.h"
#include "mocks/mock_db_client.h"
#include <memory>
#include <ctime>
#include <string>
#include <vector>

// Expose protected members for testing
class TestGenerator : public Generator {
public:
    using Generator::Generator;
    
    // Wrapper for protected method
    void Setup() { InitializeHosts(); }
    
    // Wrapper for protected method
    TelemetryRecord PublicGenerateRecord(const HostProfile& host, 
                                         std::chrono::system_clock::time_point timestamp) {
        return GenerateRecord(host, timestamp);
    }
    
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
    auto rec = gen.PublicGenerateRecord(h, now);
    
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
    h.cpu_base = 50.0;
    
    auto now = std::chrono::system_clock::now();
    auto rec = gen.PublicGenerateRecord(h, now);
    
    EXPECT_TRUE(rec.is_anomaly);
    EXPECT_NE(rec.anomaly_type.find("POINT_SPIKE"), std::string::npos);
    EXPECT_GE(rec.cpu_usage, 80.0); 
 
}

TEST(GeneratorMathTest, ContextualAnomaly) {
    telemetry::GenerateRequest req;
    req.set_tier("DELTA");
    req.set_seed(12345);
    req.mutable_anomaly_config()->set_contextual_rate(1.1); 
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run-ctx", db);
    
    HostProfile h;
    h.host_id = "test-host-ctx";
    h.cpu_base = 20.0; 
    
    // 3 AM UTC (3 hours since epoch)
    auto timestamp = std::chrono::system_clock::time_point(std::chrono::hours(3));
    
    auto rec = gen.PublicGenerateRecord(h, timestamp);
    
    EXPECT_TRUE(rec.is_anomaly);
    EXPECT_NE(rec.anomaly_type.find("CONTEXTUAL"), std::string::npos);
    EXPECT_GE(rec.cpu_usage, 80.0); 
}

TEST(GeneratorMathTest, BurstAnomalyState) {
    telemetry::GenerateRequest req;
    req.set_tier("EPSILON");
    req.set_seed(999);
    req.mutable_anomaly_config()->set_collective_rate(1.1); // Trigger immediately
    req.mutable_anomaly_config()->set_burst_duration_points(3);
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run-burst", db);
    
    HostProfile h;
    h.host_id = "test-host-burst";
    h.cpu_base = 10.0;
    h.burst_remaining = 0;
    
    // T1: Should start burst
    auto rec1 = gen.PublicGenerateRecord(h, std::chrono::system_clock::now());
    EXPECT_TRUE(rec1.is_anomaly);
    EXPECT_NE(rec1.anomaly_type.find("COLLECTIVE_BURST"), std::string::npos);
    
    // T2
    auto rec2 = gen.PublicGenerateRecord(h, std::chrono::system_clock::now());
    EXPECT_TRUE(rec2.is_anomaly);
    
    // T3
    auto rec3 = gen.PublicGenerateRecord(h, std::chrono::system_clock::now());
    EXPECT_TRUE(rec3.is_anomaly);
}

TEST(GeneratorMathTest, CorrelationAnomaly) {
    telemetry::GenerateRequest req;
    req.set_tier("ZETA");
    req.set_seed(12345);
    req.mutable_anomaly_config()->set_correlation_break_rate(1.1); // Force break
    // Removed invalid setter for duration points as it is not in MVP proto
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run-corr", db);
    
    HostProfile h;
    h.host_id = "test-host-corr";
    h.cpu_base = 10.0;
    h.correlation_broken = false; 
    
    // T1: Break starts
    auto rec1 = gen.PublicGenerateRecord(h, std::chrono::system_clock::now());
    EXPECT_TRUE(rec1.is_anomaly);
    EXPECT_NE(rec1.anomaly_type.find("CORRELATION_BREAK"), std::string::npos);
    
    if (rec1.cpu_usage < 40.0) {
        EXPECT_GT(rec1.memory_usage, 50.0);
    }
}
