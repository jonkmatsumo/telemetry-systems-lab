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
namespace api {

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
    void StartJob(const std::string& job_id, const std::string& request_id, std::function<void(const std::atomic<bool>*)> work);

    // Get status of a job
    JobStatus GetStatus(const std::string& job_id);
    
    // List all jobs
    std::vector<JobInfo> ListJobs();

    // Cancel a running job
    void CancelJob(const std::string& job_id);

    // Clean up completed jobs
    void Stop();

    // Set maximum concurrent jobs (default: 4)
    void SetMaxConcurrentJobs(size_t max_jobs);

private:
    // Internal cleanup of finished threads
    void CleanupFinishedThreads();

    std::mutex mutex_;
    std::map<std::string, JobInfo> jobs_;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> stop_flags_;
    std::map<std::string, std::thread> threads_;
    std::atomic<bool> stopping_{false};
    
    size_t max_jobs_ = 4;
    size_t current_jobs_ = 0;
};

} // namespace api
} // namespace telemetry
