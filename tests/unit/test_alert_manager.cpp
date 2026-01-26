#include <gtest/gtest.h>
#include "alert_manager.h"
#include <vector>
#include <chrono>

using namespace telemetry::anomaly;

class AlertManagerTest : public ::testing::Test {
protected:
    // Hysteresis=2, Cooldown=10s
    AlertManager manager{2, 10}; 
    std::string host = "test-host";
    std::string run_id = "run-1";
    std::chrono::system_clock::time_point start_time = std::chrono::system_clock::now();
};

TEST_F(AlertManagerTest, HysteresisLogic) {
    auto t1 = start_time;
    auto t2 = t1 + std::chrono::seconds(1);
    
    // 1st Anomaly: Should NOT alert (hysteresis check fail, pending confirmation)
    // Actually, implementation says: if (consecutive_anomalies < hysteresis_threshold_) return alerts;
    // With threshold=2:
    // 1st call: consecutive=1. 1 < 2 is true. Return empty.
    
    auto alerts1 = manager.Evaluate(host, run_id, t1, true, 5.0, false, 0.0, "d1");
    EXPECT_TRUE(alerts1.empty());

    // 2nd Anomaly: Consecutive=2. 2 < 2 is false. Proceed to alert.
    auto alerts2 = manager.Evaluate(host, run_id, t2, true, 5.0, false, 0.0, "d2");
    ASSERT_EQ(alerts2.size(), 1);
    EXPECT_EQ(alerts2[0].host_id, host);
    EXPECT_EQ(alerts2[0].severity, "MEDIUM"); // Score 5.0 < 10.0
}

TEST_F(AlertManagerTest, CooldownLogic) {
    auto t1 = start_time;
    auto t2 = t1 + std::chrono::seconds(1);
    auto t3 = t2 + std::chrono::seconds(5); // Within 10s cooldown
    auto t4 = t2 + std::chrono::seconds(11); // After 10s cooldown

    // Trigger Alert
    manager.Evaluate(host, run_id, t1, true, 15.0, false, 0.0, "d1"); // 1st
    auto alerts = manager.Evaluate(host, run_id, t2, true, 15.0, false, 0.0, "d2"); // 2nd -> Alert
    ASSERT_EQ(alerts.size(), 1);

    // 3rd Anomaly (within cooldown): Should be ignored (Hysteresis check fails as consec=1, or Cooldown fails)
    // Here consec=1 (because of reset), so invalid by hysteresis anyway.
    auto alerts3 = manager.Evaluate(host, run_id, t3, true, 15.0, false, 0.0, "d3");
    EXPECT_TRUE(alerts3.empty());

    // 4th Anomaly (after cooldown): Consec=2. Hysteresis Met. Cooldown Met.
    // Should Alert.
    auto alerts4 = manager.Evaluate(host, run_id, t4, true, 15.0, false, 0.0, "d4");
    ASSERT_EQ(alerts4.size(), 1);

    // 5th Anomaly (after cooldown): Consec=1 (reset again). No Alert.
    auto t5 = t4 + std::chrono::seconds(1);
    auto alerts5 = manager.Evaluate(host, run_id, t5, true, 15.0, false, 0.0, "d5");
    EXPECT_TRUE(alerts5.empty());
}

TEST_F(AlertManagerTest, FusionSeverity) {
    auto t = start_time + std::chrono::seconds(100);

    // Reset manager state by using a fresh host or just assume first call for this host
    std::string host2 = "host-fusion";

    // 1st call
    manager.Evaluate(host2, run_id, t, true, 4.0, true, 0.5, "d1");
    
    // 2nd call -> Alert
    // Both Detectors -> CRITICAL
    auto t2 = t + std::chrono::seconds(1);
    auto alerts = manager.Evaluate(host2, run_id, t2, true, 4.0, true, 0.5, "d2");
    ASSERT_EQ(alerts.size(), 1);
    EXPECT_EQ(alerts[0].severity, "CRITICAL");
    EXPECT_EQ(alerts[0].source, "FUSION_A_B");

    // Wait for cooldown
    auto t3 = t2 + std::chrono::seconds(15);
    
    // Just B (PCA) -> HIGH
    manager.Evaluate(host2, run_id, t3, false, 0.0, true, 0.1, "d3");
    auto alerts2 = manager.Evaluate(host2, run_id, t3+std::chrono::seconds(1), false, 0.0, true, 0.1, "d4");
    ASSERT_EQ(alerts2.size(), 1);
    EXPECT_EQ(alerts2[0].severity, "HIGH");
    EXPECT_EQ(alerts2[0].source, "DETECTOR_B_PCA");

     // Wait for cooldown
    auto t4 = t3 + std::chrono::seconds(15);

    // Just A (Stats) High Score -> HIGH
    manager.Evaluate(host2, run_id, t4, true, 20.0, false, 0.0, "d5");
    auto alerts3 = manager.Evaluate(host2, run_id, t4+std::chrono::seconds(1), true, 20.0, false, 0.0, "d6");
    ASSERT_EQ(alerts3.size(), 1);
    EXPECT_EQ(alerts3[0].severity, "HIGH");
    EXPECT_EQ(alerts3[0].source, "DETECTOR_A_STATS");
}
