#pragma once

#include "types.h"
#include "idb_client.h"
#include "telemetry.grpc.pb.h"
#include <atomic>
#include <memory>
#include <random>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>


class Generator {
public:
    Generator(const telemetry::GenerateRequest& request, 
              std::string run_id, 
              std::shared_ptr<IDbClient> db_client);
    ~Generator();

    auto Run() -> void;
    auto SetStopFlag(const std::atomic<bool>* stop_flag) -> void { stop_flag_ = stop_flag; }

protected:
    telemetry::GenerateRequest config_;
    std::string run_id_;
    std::shared_ptr<IDbClient> db_;
    const std::atomic<bool>* stop_flag_ = nullptr;

    
    std::vector<HostProfile> hosts_;
    
    auto InitializeHosts() -> void;
    auto GenerateRecord(const HostProfile& host, 
                                   std::chrono::system_clock::time_point timestamp) -> TelemetryRecord;
    
    auto WriterLoop() -> void;
    auto EnqueueBatch(std::vector<TelemetryRecord> batch) -> void;

    std::mt19937_64 rng_;

    // Bounded Queue
    std::queue<std::vector<TelemetryRecord>> write_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    size_t max_queue_size_ = 100; // batches
    std::atomic<bool> writer_running_{false};
    std::unique_ptr<std::thread> writer_thread_;
};

auto ParseTime(const std::string& iso) -> std::chrono::system_clock::time_point;