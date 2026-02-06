#pragma once

#include <pqxx/pqxx>
#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

/**
 * @brief Smart pointer for database connections that handles returning to pool.
 */
using DbConnectionPtr = std::unique_ptr<pqxx::connection, std::function<void(pqxx::connection*)>>;

/**
 * @brief Manages database connections and provides an abstraction for pooling.
 */
class DbConnectionManager {
public:
    virtual ~DbConnectionManager() = default;

    /**
     * @brief Acquires a connection from the manager.
     */
    virtual DbConnectionPtr GetConnection() = 0;

    /**
     * @brief Returns the connection string used by the manager.
     */
    virtual std::string GetConnectionString() const = 0;
};

/**
 * @brief Basic implementation that creates a new connection every time.
 */
class SimpleDbConnectionManager : public DbConnectionManager {
public:
    explicit SimpleDbConnectionManager(const std::string& conn_str) : conn_str_(conn_str) {}

    DbConnectionPtr GetConnection() override {
        return DbConnectionPtr(new pqxx::connection(conn_str_), 
                              [](pqxx::connection* c) { delete c; });
    }

    std::string GetConnectionString() const override {
        return conn_str_;
    }

private:
    std::string conn_str_;
};

/**
 * @brief Bounded connection pool for PostgreSQL connections.
 */
class PooledDbConnectionManager : public DbConnectionManager {
public:
    PooledDbConnectionManager(const std::string& conn_str, 
                               size_t pool_size, 
                               std::chrono::milliseconds acquire_timeout = std::chrono::seconds(5));
    ~PooledDbConnectionManager() override;

    DbConnectionPtr GetConnection() override;
    std::string GetConnectionString() const override { return conn_str_; }

    struct PoolStats {
        size_t size;
        size_t in_use;
        size_t available;
        long long total_acquires;
        long long total_timeouts;
    };
    PoolStats GetStats() const;

private:
    void ReleaseConnection(pqxx::connection* conn);

    std::string conn_str_;
    size_t pool_size_;
    std::chrono::milliseconds acquire_timeout_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::unique_ptr<pqxx::connection>> pool_;
    size_t in_use_count_ = 0;
    
    long long total_acquires_ = 0;
    long long total_timeouts_ = 0;

    bool shutdown_ = false;
};
