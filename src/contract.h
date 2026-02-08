#pragma once

#include <vector>
#include <string>
#include <array>
#include "types.h"

namespace telemetry::anomaly {

// Fixed-size feature vector for V1 contract
// Order: CPU, Memory, Disk, Network RX, Network TX
struct FeatureVector {
    using ValueType = double;
    static constexpr size_t kSize = 5;

    std::array<ValueType, kSize> data;

    // Direct accessors for readability
    auto cpu_usage() -> ValueType& { return data[0]; }
    [[nodiscard]] auto cpu_usage() const -> const ValueType& { return data[0]; }

    auto memory_usage() -> ValueType& { return data[1]; }
    [[nodiscard]] auto memory_usage() const -> const ValueType& { return data[1]; }

    auto disk_utilization() -> ValueType& { return data[2]; }
    [[nodiscard]] auto disk_utilization() const -> const ValueType& { return data[2]; }

    auto network_rx_rate() -> ValueType& { return data[3]; }
    [[nodiscard]] auto network_rx_rate() const -> const ValueType& { return data[3]; }

    auto network_tx_rate() -> ValueType& { return data[4]; }
    [[nodiscard]] auto network_tx_rate() const -> const ValueType& { return data[4]; }

    // Helper to populate from record
    static auto FromRecord(const TelemetryRecord& record) -> FeatureVector {
        FeatureVector v;
        v.cpu_usage() = record.cpu_usage;
        v.memory_usage() = record.memory_usage;
        v.disk_utilization() = record.disk_utilization;
        v.network_rx_rate() = record.network_rx_rate;
        v.network_tx_rate() = record.network_tx_rate;
        return v;
    }
};

struct FeatureMetadata {
    static auto GetFeatureNames() -> const std::vector<std::string>& {
        static const std::vector<std::string> names = {
            "cpu_usage",
            "memory_usage",
            "disk_utilization",
            "network_rx_rate",
            "network_tx_rate"
        };
        return names;
    }
};

} // namespace telemetry::anomaly
