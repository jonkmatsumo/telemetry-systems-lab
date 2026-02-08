#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include "idb_client.h"

namespace telemetry {

/**
 * @brief Handles detection and recovery of orphaned/stale jobs.
 */
class JobReconciler {
public:
    JobReconciler(std::shared_ptr<IDbClient> db_client, 
                  std::chrono::seconds stale_ttl = std::chrono::minutes(10));
    ~JobReconciler();

    /**
     * @brief Performs a one-time sweep of all RUNNING/QUEUED jobs.
     * Called on startup to recover from previous crashes.
     */
    void ReconcileStartup();

    /**
     * @brief Starts a background thread that periodically sweeps for stale jobs.
     */
    void Start(std::chrono::milliseconds interval = std::chrono::minutes(1));

    /**
     * @brief Stops the background sweeper.
     */
    void Stop();

private:
    void RunSweep();

    std::shared_ptr<IDbClient> db_;
    std::chrono::seconds stale_ttl_;
    
    std::atomic<bool> running_{false};
    std::mutex cv_m_;
    std::condition_variable cv_;
    std::unique_ptr<std::thread> sweeper_thread_;
};

} // namespace telemetry
