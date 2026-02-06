#include "db_connection_manager.h"
#include <spdlog/spdlog.h>

PooledDbConnectionManager::PooledDbConnectionManager(const std::string& conn_str, 
                                                     size_t pool_size, 
                                                     std::chrono::milliseconds acquire_timeout)
    : conn_str_(conn_str), 
      pool_size_(pool_size), 
      acquire_timeout_(acquire_timeout) {
    
    spdlog::info("Initializing DB connection pool with size {}", pool_size_);
}

PooledDbConnectionManager::~PooledDbConnectionManager() {
    std::unique_lock<std::mutex> lock(mutex_);
    shutdown_ = true;
    while (!pool_.empty()) {
        pool_.pop();
    }
}

DbConnectionPtr PooledDbConnectionManager::GetConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto start = std::chrono::steady_clock::now();
    
    // Wait for a connection to be available or for space to create a new one
    if (!cv_.wait_for(lock, acquire_timeout_, [this]() {
        return !pool_.empty() || in_use_count_ < pool_size_ || shutdown_;
    })) {
        total_timeouts_++;
        spdlog::error("Timeout acquiring DB connection after {}ms. Pool size: {}, In-use: {}", 
                     acquire_timeout_.count(), pool_size_, in_use_count_);
        throw std::runtime_error("DB connection acquisition timeout");
    }

    if (shutdown_) {
        throw std::runtime_error("DB Connection Manager is shutting down");
    }

    std::unique_ptr<pqxx::connection> conn;
    if (!pool_.empty()) {
        conn = std::move(pool_.front());
        pool_.pop();
    } else {
        // Create new connection if pool is not full
        try {
            conn = std::make_unique<pqxx::connection>(conn_str_);
        } catch (const std::exception& e) {
            spdlog::error("Failed to create new DB connection: {}", e.what());
            throw;
        }
    }

    in_use_count_++;
    total_acquires_++;
    
    auto end = std::chrono::steady_clock::now();
    auto wait_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    if (wait_ms > 100.0) {
        spdlog::warn("DB connection acquisition took {}ms", wait_ms);
    }

    // Return with custom deleter that returns to pool
    return DbConnectionPtr(conn.release(), [this](pqxx::connection* c) {
        this->ReleaseConnection(c);
    });
}

void PooledDbConnectionManager::ReleaseConnection(pqxx::connection* conn) {
    std::unique_lock<std::mutex> lock(mutex_);
    in_use_count_--;

    if (shutdown_ || !conn || !conn->is_open()) {
        if (conn && !conn->is_open()) {
            spdlog::warn("Dropping closed/broken DB connection");
        }
        delete conn;
        return;
    }

    pool_.push(std::unique_ptr<pqxx::connection>(conn));
    cv_.notify_one();
}

PooledDbConnectionManager::PoolStats PooledDbConnectionManager::GetStats() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return {
        pool_size_,
        in_use_count_,
        pool_.size(),
        total_acquires_,
        total_timeouts_
    };
}
