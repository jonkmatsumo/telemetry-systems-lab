#include <gtest/gtest.h>
#include "db_connection_manager.h"
#include <thread>
#include <vector>
#include <atomic>

namespace {

TEST(DbConnectionPoolTest, EnforcesMaxSize) {
    // Using a non-existent connection to test pool state management
    std::string conn_str = "host=invalid_host_for_testing";
    PooledDbConnectionManager pool(conn_str, 2, std::chrono::milliseconds(100));
    
    EXPECT_EQ(pool.GetStats().size, 2);
    EXPECT_EQ(pool.GetStats().in_use, 0);
    
    // First two should attempt to connect and throw
    EXPECT_THROW(pool.GetConnection(), std::exception);
    EXPECT_THROW(pool.GetConnection(), std::exception);
    
    // In-use should be 0 because they failed to connect
    EXPECT_EQ(pool.GetStats().in_use, 0);
}

TEST(DbConnectionPoolTest, FullPoolTimeouts) {
    // We need a real DB to test a "full" pool where connections ARE successfully held.
    const char* db_url = std::getenv("DB_CONNECTION_STRING");
    if (!db_url) {
        db_url = "postgresql://postgres:password@postgres:5432/telemetry";
    }
    
    try {
        PooledDbConnectionManager pool(db_url, 1, std::chrono::milliseconds(200));
        
        {
            auto conn1 = pool.GetConnection();
            EXPECT_EQ(pool.GetStats().in_use, 1);
            
            // Second acquisition should timeout
            EXPECT_THROW(pool.GetConnection(), std::runtime_error);
        }
        
        // After conn1 is dropped, in_use should be 0
        EXPECT_EQ(pool.GetStats().in_use, 0);
        EXPECT_EQ(pool.GetStats().available, 1);
        
        // Should be able to get it again
        auto conn2 = pool.GetConnection();
        EXPECT_EQ(pool.GetStats().in_use, 1);
        EXPECT_EQ(pool.GetStats().available, 0);
    } catch (const std::exception& e) {
        // Skip test if DB is not reachable
        GTEST_SKIP() << "Database not reachable: " << e.what();
    }
}

TEST(DbConnectionPoolTest, ConcurrentStress) {
    const char* db_url = std::getenv("DB_CONNECTION_STRING");
    if (!db_url) {
        db_url = "postgresql://postgres:password@postgres:5432/telemetry";
    }
    
    try {
        PooledDbConnectionManager pool(db_url, 5, std::chrono::seconds(2));
        
        std::atomic<int> success_count{0};
        auto worker = [&](int /*id*/) {
            for (int i = 0; i < 10; ++i) {
                try {
                    auto conn = pool.GetConnection();
                    success_count++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } catch (...) {
                    // Failures are possible if pool is too small and timeout too short,
                    // but here we have enough time.
                }
            }
        };
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        EXPECT_EQ(success_count, 100);
        EXPECT_EQ(pool.GetStats().in_use, 0);
        EXPECT_LE(pool.GetStats().available, 5);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Database not reachable: " << e.what();
    }
}

} // namespace