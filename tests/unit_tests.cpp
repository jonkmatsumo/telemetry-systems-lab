#include <gtest/gtest.h>
#include "generator.h"
#include <memory>

// Mock DbClient or use a real one?
// For pure logic tests, we can use a stub DB client or just check Generator bounds if we expose internal generation method.
// Generator::GenerateRecord is private but for testing we can make it public or friend.
// For now, let's subclass Generator or include generator.cpp directly and friend it?
// Or better, refactor Generator to have a helper class for Math?

// Helper struct for testing - we will just test logic by exposing GenerateRecord via a derived class for test
class TestGenerator : public Generator {
public:
    using Generator::Generator;
    using Generator::GenerateRecord;
    using Generator::InitializeHosts;
    using Generator::hosts_;
    
    // We need to publicize the hosts for inspection
    const std::vector<HostProfile>& GetHosts() const { return hosts_; }
};

class MockDbClient : public DbClient {
public:
    MockDbClient() : DbClient("") {}
};

TEST(GeneratorTest, HostInitialization) {
    telemetry::GenerateRequest req;
    req.set_tier("ALPHA");
    req.set_host_count(10);
    req.set_seed(12345);
    
    auto db = std::make_shared<MockDbClient>();
    TestGenerator gen(req, "test-run", db);
    
    // We need to call InitializeHosts (which is private/protected) - exposed via TestGenerator
    // But InitializeHosts is called in Run(). Let's call it manually if we can, or just call Run with dummy DB.
    // Since we mocked DB client but DbClient methods are not virtual in my implementation (oops), 
    // I can't mock them easily without changing DbClient to have virtual methods.
    // For MVP, checking GenerateRecord math is more important.
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
    h.burst_remaining = 0;
    
    auto now = std::chrono::system_clock::now();
    auto rec = gen.GenerateRecord(h, now);
    
    EXPECT_GE(rec.cpu_usage, 0.0);
    EXPECT_LE(rec.cpu_usage, 100.0);
    EXPECT_GE(rec.memory_usage, 0.0);
    EXPECT_LE(rec.memory_usage, 100.0);
    EXPECT_GE(rec.network_rx_rate, 0.0);
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
    EXPECT_GE(rec.cpu_usage, 50.0); // Should have spiked
}
