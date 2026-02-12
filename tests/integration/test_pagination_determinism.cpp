#include <gtest/gtest.h>
#include "db_client.h"
#include <vector>
#include <string>
#include <chrono>
#include <exception>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <uuid/uuid.h>

// Helper
static auto GenerateUUID() -> std::string {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    std::string out(36, '\0');
    uuid_unparse_lower(binuuid, out.data());
    return {out};
}

class PaginationTest : public ::testing::Test {
protected:
    std::string conn_str;
    
    void SetUp() override {
        const char* env_p = std::getenv("DB_CONNECTION_STRING");
        if (env_p) {
            conn_str = env_p;
        } else {
            conn_str = "postgresql://postgres:password@postgres:5432/telemetry";
        }
    }
};

TEST_F(PaginationTest, ListModelRunsDeterministic) {
    DbClient client(conn_str);
    std::string dataset_id = GenerateUUID();
    
    {
        pqxx::connection C(conn_str);
        pqxx::work W(C);
        W.exec_params(
            "INSERT INTO generation_runs (run_id, tier, host_count, start_time, end_time, interval_seconds, seed, status, config, created_at) "
            "VALUES ($1, 'TEST', 1, NOW(), NOW(), 60, 0, 'PENDING', '{}', NOW()) ON CONFLICT (run_id) DO NOTHING", 
            dataset_id
        );
        W.commit();
    }

    std::string base_time = "2025-01-01 12:00:00";
    std::vector<std::string> ids;
    {
        pqxx::connection C(conn_str);
        pqxx::work W(C);
        for(int i=0; i<5; ++i) {
            std::string id = GenerateUUID();
            ids.push_back(id);
            W.exec_params(
                "INSERT INTO model_runs (model_run_id, dataset_id, name, status, created_at, is_eligible) VALUES ($1, $2, $3, 'PENDING', $4, false)",
                id, dataset_id, "run-" + std::to_string(i), base_time
            );
        }
        W.commit();
    }

    // List with limit 2
    auto page1 = client.ListModelRuns(2, 0, "", dataset_id, "", "");
    ASSERT_EQ(page1.size(), 2);

    auto page2 = client.ListModelRuns(2, 2, "", dataset_id, "", "");
    ASSERT_EQ(page2.size(), 2);
    
    auto page3 = client.ListModelRuns(2, 4, "", dataset_id, "", "");
    ASSERT_EQ(page3.size(), 1);

    std::vector<std::string> fetched_ids;
    for (const auto& item : page1) {
        fetched_ids.push_back(item["model_run_id"]);
    }
    for (const auto& item : page2) {
        fetched_ids.push_back(item["model_run_id"]);
    }
    for (const auto& item : page3) {
        fetched_ids.push_back(item["model_run_id"]);
    }
    
    ASSERT_EQ(fetched_ids.size(), 5);
    
    // Sort original IDs descending (secondary sort key)
    std::sort(ids.begin(), ids.end(), std::greater<>());
    
    EXPECT_EQ(fetched_ids, ids);
}
