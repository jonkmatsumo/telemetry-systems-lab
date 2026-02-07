#include "job_reconciler.h"
#include <spdlog/spdlog.h>

namespace telemetry {

JobReconciler::JobReconciler(std::shared_ptr<IDbClient> db_client, 
                             std::chrono::seconds stale_ttl)
    : db_(std::move(db_client)), stale_ttl_(stale_ttl) {}

JobReconciler::~JobReconciler() {
    Stop();
}

void JobReconciler::ReconcileStartup() {
    spdlog::info("Running startup job reconciliation...");
    db_->ReconcileStaleJobs(std::nullopt); // Reconcile all RUNNING/PENDING
}

void JobReconciler::Start(std::chrono::milliseconds interval) {
    if (running_) return;
    running_ = true;
    sweeper_thread_ = std::make_unique<std::thread>([this, interval]() {
        spdlog::info("JobReconciler periodic sweeper started (interval={}ms, TTL={}s).", 
                     std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(), 
                     stale_ttl_.count());
        while (running_) {
            std::this_thread::sleep_for(interval);
            if (!running_) break;
            this->RunSweep();
        }
        spdlog::info("JobReconciler periodic sweeper stopped.");
    });
}

void JobReconciler::Stop() {
    running_ = false;
    if (sweeper_thread_ && sweeper_thread_->joinable()) {
        sweeper_thread_->join();
    }
    sweeper_thread_.reset();
}

void JobReconciler::RunSweep() {
    try {
        db_->ReconcileStaleJobs(stale_ttl_);
    } catch (const std::exception& e) {
        spdlog::error("JobReconciler sweep failed: {}", e.what());
    }
}

} // namespace telemetry
