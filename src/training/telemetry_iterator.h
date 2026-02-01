#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "linalg/matrix.h"

namespace telemetry {
namespace training {

class TelemetryBatchIterator {
public:
    TelemetryBatchIterator(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           size_t batch_size);

    bool NextBatch(std::vector<linalg::Vector>& out_batch);
    void Reset();
    size_t TotalRowsProcessed() const;

private:
    std::string db_conn_str_;
    std::string dataset_id_;
    size_t batch_size_;
    int64_t last_record_id_;
    size_t total_processed_;
};

} // namespace training
} // namespace telemetry
