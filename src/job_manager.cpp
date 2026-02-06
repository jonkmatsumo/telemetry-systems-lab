#include "job_manager.h"
#include "metrics.h"
#include <spdlog/spdlog.h>

namespace telemetry {
namespace api {

JobManager::JobManager() {}

JobManager::~JobManager() {
    Stop();
}

void JobManager::SetMaxConcurrentJobs(size_t max_jobs) {
    std::lock_guard<std::mutex> lk(mutex_);
    max_jobs_ = max_jobs;
}

void JobManager::CleanupFinishedThreads() {
    for (auto it = threads_.begin(); it != threads_.end(); ) {
        if (jobs_.count(it->first) && jobs_[it->first].status != JobStatus::RUNNING) {
            if (it->second.joinable()) {
                it->second.join();
            }
            stop_flags_.erase(it->first);
            it = threads_.erase(it);
        } else {
            ++it;
        }
    }
}

void JobManager::StartJob(const std::string& job_id, const std::string& request_id, std::function<void(const std::atomic<bool>*)> work) {
    std::lock_guard<std::mutex> lk(mutex_);
    
    
    if (stopping_) {
        throw std::runtime_error("JobManager is stopping");
    }

    CleanupFinishedThreads();

    if (current_jobs_ >= max_jobs_) {
        telemetry::metrics::MetricsRegistry::Instance().Increment("job_rejected_total", {{"reason", "resource_exhausted"}});
        throw std::runtime_error("Job queue full: max concurrent jobs reached");
    }

    auto stop_flag = std::make_shared<std::atomic<bool>>(false);
    stop_flags_[job_id] = stop_flag;

    JobInfo info;
    info.job_id = job_id;
    info.request_id = request_id;
    info.status = JobStatus::RUNNING;
    jobs_[job_id] = info;
    current_jobs_++;
    telemetry::metrics::MetricsRegistry::Instance().SetGauge("job_active_count", static_cast<double>(current_jobs_));

    threads_[job_id] = std::thread([this, job_id, request_id, work, stop_flag]() {
        try {
            spdlog::info("DEBUG: Starting job wrapper for {}", job_id);
            work(stop_flag.get());
            spdlog::info("DEBUG: Job wrapper finished for {}", job_id);
            
            std::lock_guard<std::mutex> lk(mutex_);
            if (jobs_.count(job_id)) {
                if (stop_flag->load()) {
                    jobs_[job_id].status = JobStatus::CANCELLED;
                } else {
                    jobs_[job_id].status = JobStatus::COMPLETED;
                }
            }
            current_jobs_--;
            telemetry::metrics::MetricsRegistry::Instance().SetGauge("job_active_count", static_cast<double>(current_jobs_));
            telemetry::metrics::MetricsRegistry::Instance().Increment("job_completed_total", {});
        } catch (const std::exception& e) {
            spdlog::error("Job {} (req_id: {}) failed: {}", job_id, request_id, e.what());
            // ... (rest of catch)
            std::lock_guard<std::mutex> lk(mutex_);
            if (jobs_.count(job_id)) {
                jobs_[job_id].status = JobStatus::FAILED;
                jobs_[job_id].error = e.what();
            }
            current_jobs_--;
            telemetry::metrics::MetricsRegistry::Instance().SetGauge("job_active_count", static_cast<double>(current_jobs_));
            telemetry::metrics::MetricsRegistry::Instance().Increment("job_failed_total", {{"error", "exception"}});
        } catch (...) {
            spdlog::error("Job {} (req_id: {}) failed with unknown exception", job_id, request_id);
            std::lock_guard<std::mutex> lk(mutex_);
            if (jobs_.count(job_id)) {
                jobs_[job_id].status = JobStatus::FAILED;
                jobs_[job_id].error = "Unknown exception";
            }
            current_jobs_--;
            telemetry::metrics::MetricsRegistry::Instance().SetGauge("job_active_count", static_cast<double>(current_jobs_));
            telemetry::metrics::MetricsRegistry::Instance().Increment("job_failed_total", {{"error", "unknown"}});
        }
    });
}

void JobManager::CancelJob(const std::string& job_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (stop_flags_.count(job_id)) {
        stop_flags_[job_id]->store(true);
        spdlog::info("Requested stop for job {}", job_id);
    }
}

JobStatus JobManager::GetStatus(const std::string& job_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (jobs_.count(job_id)) {
        return jobs_[job_id].status;
    }
    return JobStatus::CANCELLED; // Or NOT_FOUND
}

std::vector<JobInfo> JobManager::ListJobs() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<JobInfo> out;
    for (const auto& kv : jobs_) {
        out.push_back(kv.second);
    }
    return out;
}

void JobManager::Stop() {
    if (stopping_.exchange(true)) return;
    
    spdlog::info("Stopping JobManager, waiting for {} threads...", threads_.size());
    
    std::vector<std::thread> to_join;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [id, flag] : stop_flags_) {
            flag->store(true);
        }
        
        for (auto& [id, t] : threads_) {
            if (t.joinable()) {
                to_join.push_back(std::move(t));
            }
        }
        threads_.clear();
        stop_flags_.clear();
    }

    for (auto& t : to_join) {
        t.join();
    }
}

} // namespace api
} // namespace telemetry
