#include <gtest/gtest.h>
#include "training/pca_trainer.h"
#include "mocks/mock_db_client.h"
#include <cstdlib>

namespace {

TEST(PcaMemoryGuardrailsTest, RejectsLargeDataset) {
    // 1. Setup Mock DB with many rows
    auto mock_db = std::make_shared<MockDbClient>();
    mock_db->mock_record_count = 10000000; // 10M rows
    
    // 2. Set memory limit to 64MB (should be exceeded by 10M doubles + margin)
    // 10M * 8 bytes = 80MB
    setenv("PCA_TRAIN_MAX_MEMORY_MB", "64", 1);
    
    auto manager = mock_db->GetConnectionManager();
    
    // 3. Verify it throws
    EXPECT_THROW({
        try {
            telemetry::training::TrainPcaFromDbBatched(std::static_pointer_cast<IDbClient>(mock_db), "some-dataset", 3, 99.5, 100);
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("Training rejected"), std::string::npos);
            throw;
        }
    }, std::runtime_error);
    
    unsetenv("PCA_TRAIN_MAX_MEMORY_MB");
}

TEST(PcaMemoryGuardrailsTest, AcceptsSmallDataset) {
    auto mock_db = std::make_shared<MockDbClient>();
    mock_db->mock_record_count = 1000; // 1000 rows
    
    setenv("PCA_TRAIN_MAX_MEMORY_MB", "256", 1);
    
    // This should proceed past pre-flight check.
    // It will fail later because TelemetryBatchIterator will try to use the mock manager's "dummy" connection.
    // But we only care about the pre-flight check for this test.
    
    try {
        telemetry::training::TrainPcaFromDbBatched(std::static_pointer_cast<IDbClient>(mock_db), "some-dataset", 3, 99.5, 100);
    } catch (const std::exception& e) {
        // It might throw due to connection failure, but it shouldn't be a rejection error.
        EXPECT_EQ(std::string(e.what()).find("Training rejected"), std::string::npos);
    }
    
    unsetenv("PCA_TRAIN_MAX_MEMORY_MB");
}

} // namespace
