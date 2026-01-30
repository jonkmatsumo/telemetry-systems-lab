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

    // Start a new background job
    void StartJob(const std::string& job_id, const std::string& request_id, std::function<void()> work);

    // Get status of a job
    JobStatus GetStatus(const std::string& job_id);
    
    // List all jobs
    std::vector<JobInfo> ListJobs();

    // Clean up completed jobs
    void Stop();

    // Set maximum concurrent jobs (default: 4)
    void SetMaxConcurrentJobs(size_t max_jobs);

private:
    std::mutex mutex_;
    std::map<std::string, JobInfo> jobs_;
    std::vector<std::thread> threads_;
    std::atomic<bool> stopping_{false};
    
    size_t max_jobs_ = 4;
    size_t current_jobs_ = 0;
};

} // namespace api
} // namespace telemetry
