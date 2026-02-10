#include <gtest/gtest.h>
#include "training/pca_trainer.h"
#include "mocks/mock_db_client.h"
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace {

class InMemoryBatchSource final : public telemetry::training::ITelemetryBatchSource {
public:
    InMemoryBatchSource(std::vector<telemetry::linalg::Vector> samples, size_t batch_size)
        : samples_(std::move(samples)), batch_size_(batch_size) {}

    auto NextBatch(std::vector<telemetry::linalg::Vector>& out_batch) -> bool override {
        out_batch.clear();
        if (cursor_ >= samples_.size()) {
            return false;
        }

        const size_t end = std::min(samples_.size(), cursor_ + batch_size_);
        out_batch.reserve(end - cursor_);
        for (size_t idx = cursor_; idx < end; ++idx) {
            out_batch.push_back(samples_[idx]);
        }

        cursor_ = end;
        total_processed_ += out_batch.size();
        return true;
    }

    auto Reset() -> void override {
        cursor_ = 0;
        total_processed_ = 0;
    }

    [[nodiscard]] auto TotalRowsProcessed() const -> size_t override {
        return total_processed_;
    }

private:
    std::vector<telemetry::linalg::Vector> samples_;
    size_t batch_size_;
    size_t cursor_ = 0;
    size_t total_processed_ = 0;
};

auto BuildSyntheticSamples(size_t count) -> std::vector<telemetry::linalg::Vector> {
    std::vector<telemetry::linalg::Vector> samples;
    samples.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        telemetry::linalg::Vector row(5);
        row[0] = 20.0 + static_cast<double>(i % 10);
        row[1] = 0.7 * row[0] + 3.0;
        row[2] = 40.0 + static_cast<double>(i % 5);
        row[3] = 10.0 + static_cast<double>((i * 3) % 7);
        row[4] = 0.5 * row[3] + 1.0;
        samples.push_back(std::move(row));
    }
    return samples;
}

TEST(PcaMemoryGuardrailsTest, RejectsLargeDataset) {
    // 1. Setup Mock DB with many rows
    auto mock_db = std::make_shared<MockDbClient>();
    mock_db->mock_record_count = 10000000; // 10M rows
    
    // 2. Set memory limit to 64MB (should be exceeded by 10M doubles + margin)
    // 10M * 8 bytes = 80MB
    setenv("PCA_TRAIN_MAX_MEMORY_MB", "64", 1);
    
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

    const auto samples = BuildSyntheticSamples(64);
    telemetry::training::TelemetryBatchSourceFactory batch_source_factory =
        [samples](const std::string&, size_t batch_size) {
            return std::make_unique<InMemoryBatchSource>(samples, batch_size);
        };

    telemetry::training::PcaArtifact artifact;
    EXPECT_NO_THROW({
        artifact = telemetry::training::TrainPcaFromDbBatched(std::static_pointer_cast<IDbClient>(mock_db),
                                                               "some-dataset",
                                                               3,
                                                               99.5,
                                                               100,
                                                               nullptr,
                                                               batch_source_factory);
    });
    EXPECT_EQ(artifact.n_components, 3);

    unsetenv("PCA_TRAIN_MAX_MEMORY_MB");
}

} // namespace
