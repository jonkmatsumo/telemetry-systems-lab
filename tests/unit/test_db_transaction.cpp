#include <gtest/gtest.h>
#include "db_client.h"
#include "db_transaction.h"
#include <uuid/uuid.h>

namespace {

auto GenerateUUID() -> std::string {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    std::string out(36, '\0');
    uuid_unparse_lower(binuuid, out.data());
    return {out};
}

class TransactionScopeTest : public ::testing::Test {
protected:
    std::string conn_str;
    
    void SetUp() override {
        const char* env_p = std::getenv("DB_CONNECTION_STRING");
        if (env_p) {
            conn_str = env_p;
        } else {
            conn_str = "postgresql://postgres:password@postgres:5432/telemetry";
        }
        
        // Verify reachability
        try {
            pqxx::connection conn(conn_str);
        } catch (...) {
            GTEST_SKIP() << "Database not reachable for transaction tests";
        }
    }
};

TEST_F(TransactionScopeTest, RollbackOnDestruction) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    {
        auto scope = client.BeginTransaction("TestRollback");
        
        // We use raw SQL to avoid depending on specific DbClient methods that might change
        scope->txn().exec_params(
            "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config) "
            "VALUES ($1, 'TEST', 1, NOW(), NOW(), 60, 0, 'PENDING', '{}')", 
            run_id);
        
        // No scope->commit() called here
    }
    
    // Verify it's not there
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_TRUE(detail.empty() || detail.is_null());
}

TEST_F(TransactionScopeTest, RollbackOnException) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    try {
        auto scope = client.BeginTransaction("TestExceptionRollback");
        
        scope->txn().exec_params(
            "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config) "
            "VALUES ($1, 'TEST', 1, NOW(), NOW(), 60, 0, 'PENDING', '{}')", 
            run_id);
            
        throw std::runtime_error("Simulated failure");
        
        scope->commit();
    } catch (const std::runtime_error&) {
        // Expected
    }
    
    // Verify it's not there
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_TRUE(detail.empty() || detail.is_null());
}

TEST_F(TransactionScopeTest, CommitPersists) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    {
        auto scope = client.BeginTransaction("TestCommit");
        
        scope->txn().exec_params(
            "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config) "
            "VALUES ($1, 'TEST', 1, NOW(), NOW(), 60, 0, 'PENDING', '{}')", 
            run_id);
            
        scope->commit();
    }
    
    // Verify it IS there
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_EQ(detail["run_id"], run_id);
}

} // namespace
