#pragma once

#include <pqxx/pqxx>
#include <memory>
#include <string>

/**
 * @brief Manages database connections and provides an abstraction for pooling.
 */
class DbConnectionManager {
public:
    virtual ~DbConnectionManager() = default;

    /**
     * @brief Acquires a connection from the manager.
     * In the non-pooling implementation, this may create a new connection.
     * In a pooling implementation, this will block or timeout until a connection is available.
     */
    virtual std::unique_ptr<pqxx::connection> GetConnection() = 0;

    /**
     * @brief Returns the connection string used by the manager.
     */
    virtual std::string GetConnectionString() const = 0;
};

/**
 * @brief Basic implementation that creates a new connection every time.
 * This maintains current behavior while introducing the abstraction.
 */
class SimpleDbConnectionManager : public DbConnectionManager {
public:
    explicit SimpleDbConnectionManager(const std::string& conn_str) : conn_str_(conn_str) {}

    std::unique_ptr<pqxx::connection> GetConnection() override {
        return std::make_unique<pqxx::connection>(conn_str_);
    }

    std::string GetConnectionString() const override {
        return conn_str_;
    }

private:
    std::string conn_str_;
};
