#pragma once

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <functional>
#include <memory>
#include <atomic>

namespace telemetry {

enum class JobStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct JobInfo {
    std::string job_id;
    std::string request_id;
    JobStatus status;
    std::string error;
    // Add more metadata if needed
};

class JobManager {
public:
    JobManager();
    ~JobManager();

    // Start a new background job. The work function receives a pointer to an atomic bool for cancellation check.
    auto StartJob(const std::string& job_id, const std::string& request_id, const std::function<void(const std::atomic<bool>*)>& work) -> void;

    // Get status of a job
    auto GetStatus(const std::string& job_id) -> JobStatus;
    
    // List all jobs
    auto ListJobs() -> std::vector<JobInfo>;

    // Cancel a running job
    auto CancelJob(const std::string& job_id) -> void;

    // Clean up completed jobs
    auto Stop() -> void;

    // Set maximum concurrent jobs (default: 4)
    auto SetMaxConcurrentJobs(size_t max_jobs) -> void;

private:
    // Internal cleanup of finished threads
    auto CleanupFinishedThreads() -> void;

    std::mutex mutex_;
    std::map<std::string, JobInfo> jobs_;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> stop_flags_;
    std::map<std::string, std::thread> threads_;
    std::atomic<bool> stopping_{false};
    
    size_t max_jobs_ = 4;
    size_t current_jobs_ = 0;
};

namespace api {
    using JobStatus = ::telemetry::JobStatus;
    using JobInfo = ::telemetry::JobInfo;
    using JobManager = ::telemetry::JobManager;
}

} // namespace telemetry