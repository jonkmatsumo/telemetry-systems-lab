#include <gtest/gtest.h>
#include "db_client.h"
#include "job_reconciler.h"
#include <uuid/uuid.h>
#include <thread>

namespace {

auto GenerateUUID() -> std::string {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    std::string out(36, '\0');
    uuid_unparse_lower(binuuid, out.data());
    return {out};
}

class HeartbeatReconciliationTest : public ::testing::Test {
protected:
    std::string conn_str;
    
    void SetUp() override {
        const char* env_p = std::getenv("DB_CONNECTION_STRING");
        if (env_p) {
            conn_str = env_p;
        } else {
            conn_str = "postgresql://postgres:password@postgres:5432/telemetry";
        }
        
        try {
            pqxx::connection conn(conn_str);
        } catch (...) {
            GTEST_SKIP() << "Database not reachable for heartbeat tests";
        }
    }
};

TEST_F(HeartbeatReconciliationTest, DetectsStaleHeartbeat) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    // Create a job with an old heartbeat
    {
        auto scope = client.BeginTransaction("TestStaleHeartbeat");
        scope->txn().exec_params(
            "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config, heartbeat_at) "
            "VALUES ($1, 'TEST', 1, NOW(), NOW(), 60, 0, 'RUNNING', '{}', NOW() - INTERVAL '30 seconds')", 
            run_id);
        scope->commit();
    }
    
    // Reconcile with 10s TTL
    client.ReconcileStaleJobs(std::chrono::seconds(10));
    
    // Verify it failed
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_EQ(detail["status"], "FAILED");
    EXPECT_EQ(detail["error"], "Stale job detected (heartbeat timeout)");
}

TEST_F(HeartbeatReconciliationTest, IgnoresFreshHeartbeat) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    // Create a fresh job
    {
        auto scope = client.BeginTransaction("TestFreshHeartbeat");
        scope->txn().exec_params(
            "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config, heartbeat_at) "
            "VALUES ($1, 'TEST', 1, NOW(), NOW(), 60, 0, 'RUNNING', '{}', NOW())", 
            run_id);
        scope->commit();
    }
    
    // Reconcile with 10s TTL
    client.ReconcileStaleJobs(std::chrono::seconds(10));
    
    // Verify it is still RUNNING
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_EQ(detail["status"], "RUNNING");
}

} // namespace
