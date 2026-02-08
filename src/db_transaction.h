#pragma once

#include <pqxx/pqxx>
#include <memory>
#include <chrono>
#include <string>
#include "db_connection_manager.h"
#include "obs/metrics.h"
#include <spdlog/spdlog.h>

namespace telemetry::db {

/**
 * @brief RAII wrapper for database transactions.
 * Ensures that a transaction is either committed or rolled back.
 * Also captures telemetry about transaction duration and outcome.
 */
class TransactionScope {
public:
    explicit TransactionScope(std::shared_ptr<DbConnectionManager> manager, std::string name = "unnamed")
        : name_(std::move(name)), 
          conn_ptr_(manager->GetConnection()), 
          work_(*conn_ptr_) {
        start_time_ = std::chrono::steady_clock::now();
    }

    ~TransactionScope() {
        if (!committed_) {
            try {
                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration<double, std::milli>(end_time - start_time_).count();
                
                telemetry::obs::EmitCounter("db_transaction_rollbacks_total", 1, "count", "db", {{"name", name_}});
                telemetry::obs::EmitHistogram("db_transaction_duration_ms", duration, "ms", "db", {{"name", name_}, {"outcome", "rollback"}});
                
                spdlog::warn("TransactionScope '{}' rolled back on destruction", name_);
            } catch (...) {
                // Destructors should not throw
            }
        }
    }

    TransactionScope(const TransactionScope&) = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;
    TransactionScope(TransactionScope&&) = delete;
    TransactionScope& operator=(TransactionScope&&) = delete;

    void commit() {
        if (committed_) {
            return;
        }
        
        work_.commit();
        committed_ = true;
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end_time - start_time_).count();
        
        telemetry::obs::EmitCounter("db_transaction_commits_total", 1, "count", "db", {{"name", name_}});
        telemetry::obs::EmitHistogram("db_transaction_duration_ms", duration, "ms", "db", {{"name", name_}, {"outcome", "commit"}});
    }

    pqxx::work& txn() { return work_; }
    pqxx::connection& conn() { return *conn_ptr_; }

private:
    std::string name_;
    DbConnectionPtr conn_ptr_;
    pqxx::work work_;
    bool committed_ = false;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace telemetry::db
