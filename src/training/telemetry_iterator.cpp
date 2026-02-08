#include "training/telemetry_iterator.h"
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

// Compatibility macro for libpqxx 6.x vs 7.x
#if !defined(PQXX_VERSION_MAJOR) || (PQXX_VERSION_MAJOR < 7)
#define PQXX_EXEC_PREPPED(txn, stmt, ...) (txn).exec_prepared(stmt, ##__VA_ARGS__) // NOLINT(clang-diagnostic-gnu-zero-variadic-macro-arguments)
#else
#define PQXX_EXEC_PREPPED(txn, stmt, ...) (txn).exec(pqxx::prepped{stmt}, pqxx::params{__VA_ARGS__}) // NOLINT(clang-diagnostic-gnu-zero-variadic-macro-arguments)
#endif

namespace telemetry::training {

TelemetryBatchIterator::TelemetryBatchIterator(std::shared_ptr<DbConnectionManager> manager,
                                               std::string dataset_id,
                                               size_t batch_size)
    : manager_(std::move(manager)),
      dataset_id_(std::move(dataset_id)),
      batch_size_(batch_size),
      last_record_id_(0),
      total_processed_(0) {}

auto TelemetryBatchIterator::NextBatch(std::vector<linalg::Vector>& out_batch) -> bool {
    out_batch.clear();
    try {
        auto C_ptr = manager_->GetConnection(); pqxx::connection& C = *C_ptr;
        pqxx::read_transaction R(C);

        // Using keyset pagination: ORDER BY record_id LIMIT N
        // We filter by run_id (dataset_id) and record_id > last_record_id_
        pqxx::result res = PQXX_EXEC_PREPPED(R, "get_telemetry_batch",
            dataset_id_,
            last_record_id_,
            batch_size_
        );

        if (res.empty()) {
            return false;
        }

        for (const auto& row : res) {
            linalg::Vector v(5);
            v[0] = row["cpu_usage"].as<double>();
            v[1] = row["memory_usage"].as<double>();
            v[2] = row["disk_utilization"].as<double>();
            v[3] = row["network_rx_rate"].as<double>();
            v[4] = row["network_tx_rate"].as<double>();
            out_batch.push_back(std::move(v));
            last_record_id_ = row["record_id"].as<int64_t>();
        }

        total_processed_ += out_batch.size();
        return true;

    } catch (const std::exception& e) {
        spdlog::error("TelemetryBatchIterator error: {}", e.what());
        return false;
    }
}

auto TelemetryBatchIterator::Reset() -> void {
    last_record_id_ = 0;
    total_processed_ = 0;
}

auto TelemetryBatchIterator::TotalRowsProcessed() const -> size_t {
    return total_processed_;
}

} // namespace telemetry::training
