#include "job_reconciler.h"
#include <spdlog/spdlog.h>

namespace telemetry {

JobReconciler::JobReconciler(std::shared_ptr<IDbClient> db_client, 
                             std::chrono::seconds stale_ttl)
    : db_(std::move(db_client)), stale_ttl_(stale_ttl) {}

JobReconciler::~JobReconciler() {
    Stop();
}

auto JobReconciler::ReconcileStartup() -> void {
    spdlog::info("Running startup job reconciliation...");
    db_->ReconcileStaleJobs(std::nullopt); // Reconcile all RUNNING/PENDING
}

auto JobReconciler::Start(std::chrono::milliseconds interval) -> void {
    if (running_) { return; }
    running_ = true;
    sweeper_thread_ = std::make_unique<std::thread>([this, interval]() {
        spdlog::info("JobReconciler periodic sweeper started (interval={}ms, TTL={}s).", 
                     interval.count(), stale_ttl_.count());
        while (running_) {
            std::unique_lock<std::mutex> lock(cv_m_);
            if (cv_.wait_for(lock, interval, [this]() { return !running_.load(); })) {
                break; // running_ was set to false
            }
            this->RunSweep();
        }
        spdlog::info("JobReconciler periodic sweeper stopped.");
    });
}

auto JobReconciler::Stop() -> void {
    running_ = false;
    cv_.notify_all();
    if (sweeper_thread_ && sweeper_thread_->joinable()) {
        sweeper_thread_->join();
    }
    sweeper_thread_.reset();
}

auto JobReconciler::RunSweep() -> void {
    try {
        db_->ReconcileStaleJobs(stale_ttl_);
    } catch (const std::exception& e) {
        spdlog::error("JobReconciler sweep failed: {}", e.what());
    }
}

} // namespace telemetry
