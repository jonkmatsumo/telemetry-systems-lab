#include <gtest/gtest.h>
#include "training/telemetry_iterator.h"

namespace telemetry {
namespace training {

TEST(TelemetryIteratorTest, InitialState) {
    auto manager = std::make_shared<SimpleDbConnectionManager>("dbname=test");
    std::string dataset_id = "test-dataset";
    size_t batch_size = 100;
    
    TelemetryBatchIterator iter(manager, dataset_id, batch_size);
    
    EXPECT_EQ(iter.TotalRowsProcessed(), 0);
}

TEST(TelemetryIteratorTest, ResetState) {
    auto manager = std::make_shared<SimpleDbConnectionManager>("conn");
    TelemetryBatchIterator iter(manager, "dataset", 100);
    
    // Logic to simulate some processing could go here if NextBatch wasn't a stub
    iter.Reset();
    
    EXPECT_EQ(iter.TotalRowsProcessed(), 0);
}

} // namespace training
} // namespace telemetry
