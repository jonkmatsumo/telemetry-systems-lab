#include <gtest/gtest.h>
#include "training/telemetry_iterator.h"

namespace telemetry {
namespace training {

TEST(TelemetryIteratorTest, InitialState) {
    std::string db_conn = "dbname=test";
    std::string dataset_id = "test-dataset";
    size_t batch_size = 100;
    
    TelemetryBatchIterator iter(db_conn, dataset_id, batch_size);
    
    EXPECT_EQ(iter.TotalRowsProcessed(), 0);
}

TEST(TelemetryIteratorTest, ResetState) {
    TelemetryBatchIterator iter("conn", "dataset", 100);
    
    // Logic to simulate some processing could go here if NextBatch wasn't a stub
    iter.Reset();
    
    EXPECT_EQ(iter.TotalRowsProcessed(), 0);
}

} // namespace training
} // namespace telemetry
