#include "job_manager.h"
#include <spdlog/spdlog.h>

namespace telemetry {
namespace api {

JobManager::JobManager() {}

JobManager::~JobManager() {
    Stop();
}

void JobManager::StartJob(const std::string& job_id, std::function<void()> work) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    JobInfo info;
    info.job_id = job_id;
    info.status = JobStatus::RUNNING;
    jobs_[job_id] = info;

    threads_.emplace_back([this, job_id, work]() {
        try {
            work();
            
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.count(job_id)) {
                jobs_[job_id].status = JobStatus::COMPLETED;
            }
        } catch (const std::exception& e) {
            spdlog::error("Job {} failed: {}", job_id, e.what());
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.count(job_id)) {
                jobs_[job_id].status = JobStatus::FAILED;
                jobs_[job_id].error = e.what();
            }
        }
    });
}

JobStatus JobManager::GetStatus(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.count(job_id)) {
        return jobs_[job_id].status;
    }
    return JobStatus::CANCELLED; // Or NOT_FOUND
}

std::vector<JobInfo> JobManager::ListJobs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<JobInfo> out;
    for (const auto& kv : jobs_) {
        out.push_back(kv.second);
    }
    return out;
}

void JobManager::Stop() {
    if (stopping_.exchange(true)) return;
    
    spdlog::info("Stopping JobManager, waiting for {} threads...", threads_.size());
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
}

} // namespace api
} // namespace telemetry
