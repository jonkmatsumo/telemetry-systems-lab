#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "linalg/matrix.h"
#include "db_connection_manager.h"

namespace telemetry {
namespace training {

class TelemetryBatchIterator {
public:
    TelemetryBatchIterator(std::shared_ptr<DbConnectionManager> manager,
                           const std::string& dataset_id,
                           size_t batch_size);

    bool NextBatch(std::vector<linalg::Vector>& out_batch);
    void Reset();
    size_t TotalRowsProcessed() const;

private:
    std::shared_ptr<DbConnectionManager> manager_;
    std::string dataset_id_;
    size_t batch_size_;
    int64_t last_record_id_;
    size_t total_processed_;
};

} // namespace training
} // namespace telemetry
