#include "training/telemetry_iterator.h"

namespace telemetry {
namespace training {

TelemetryBatchIterator::TelemetryBatchIterator(const std::string& db_conn_str,
                                               const std::string& dataset_id,
                                               size_t batch_size)
    : db_conn_str_(db_conn_str),
      dataset_id_(dataset_id),
      batch_size_(batch_size),
      last_record_id_(0),
      total_processed_(0) {}

bool TelemetryBatchIterator::NextBatch(std::vector<linalg::Vector>& out_batch) {
    out_batch.clear();
    // Stubbed for now
    return false;
}

void TelemetryBatchIterator::Reset() {
    last_record_id_ = 0;
    total_processed_ = 0;
}

size_t TelemetryBatchIterator::TotalRowsProcessed() const {
    return total_processed_;
}

} // namespace training
} // namespace telemetry
