#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "linalg/matrix.h"
#include "db_connection_manager.h"

namespace telemetry::training {

class TelemetryBatchIterator {
public:
    TelemetryBatchIterator(std::shared_ptr<DbConnectionManager> manager,
                           std::string dataset_id,
                           size_t batch_size);

    auto NextBatch(std::vector<linalg::Vector>& out_batch) -> bool;
    auto Reset() -> void;
    [[nodiscard]] auto TotalRowsProcessed() const -> size_t;

private:
    std::shared_ptr<DbConnectionManager> manager_;
    std::string dataset_id_;
    size_t batch_size_;
    int64_t last_record_id_ = 0;
    size_t total_processed_ = 0;
};

} // namespace telemetry::training
