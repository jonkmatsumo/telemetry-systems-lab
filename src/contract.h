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
    ValueType& cpu_usage() { return data[0]; }
    const ValueType& cpu_usage() const { return data[0]; }

    ValueType& memory_usage() { return data[1]; }
    const ValueType& memory_usage() const { return data[1]; }

    ValueType& disk_utilization() { return data[2]; }
    const ValueType& disk_utilization() const { return data[2]; }

    ValueType& network_rx_rate() { return data[3]; }
    const ValueType& network_rx_rate() const { return data[3]; }

    ValueType& network_tx_rate() { return data[4]; }
    const ValueType& network_tx_rate() const { return data[4]; }

    // Helper to populate from record
    static FeatureVector FromRecord(const TelemetryRecord& record) {
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
    static const std::vector<std::string>& GetFeatureNames() {
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
