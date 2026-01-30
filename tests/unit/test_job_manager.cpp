#include <gtest/gtest.h>
#include "job_manager.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

using namespace telemetry::api;

class JobManagerTest : public ::testing::Test {
protected:
    JobManager manager;
};

TEST_F(JobManagerTest, EnforcesConcurrencyLimit) {
    manager.SetMaxConcurrentJobs(1);

    // Start a job that sleeps to hold the slot
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
    bool proceed = false;

    manager.StartJob("job1", "req1", [&](const std::atomic<bool>* stop_flag) {
        std::unique_lock<std::mutex> lock(mtx);
        ready = true;
        cv.notify_one();
        cv.wait(lock, [&]() { return proceed || stop_flag->load(); });
    });

    // Wait for job1 to start
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return ready; });
    }

    // Try to start job2, should fail
    EXPECT_THROW({
        manager.StartJob("job2", "req2", [](const std::atomic<bool>*) {});
    }, std::runtime_error);

    // Release job1
    {
        std::lock_guard<std::mutex> lock(mtx);
        proceed = true;
    }
    cv.notify_one();
    
    // Ensure jobs finish before locals (mtx, cv) are destroyed
    manager.Stop();
}

TEST_F(JobManagerTest, AllowsJobAfterCompletion) {
    manager.SetMaxConcurrentJobs(1);

    manager.StartJob("job1", "req1", [](const std::atomic<bool>*) {
        // Quick job
    });

    // Wait slightly for job1 to finish
    int retries = 100;
    while (retries-- > 0) {
        if (manager.GetStatus("job1") == JobStatus::COMPLETED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(manager.GetStatus("job1"), JobStatus::COMPLETED);

    // Should succeed now
    EXPECT_NO_THROW({
        manager.StartJob("job2", "req2", [](const std::atomic<bool>*) {});
    });
}

TEST_F(JobManagerTest, CanCancelJob) {
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
    bool cancelled = false;

    manager.StartJob("job1", "req1", [&](const std::atomic<bool>* stop_flag) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            ready = true;
        }
        cv.notify_one();
        
        while (!stop_flag->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        cancelled = true;
    });

    // Wait for job1 to start
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return ready; });
    }

    EXPECT_EQ(manager.GetStatus("job1"), JobStatus::RUNNING);
    
    manager.CancelJob("job1");

    // Wait for job1 to register cancellation
    int retries = 100;
    while (retries-- > 0) {
        if (manager.GetStatus("job1") == JobStatus::CANCELLED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_EQ(manager.GetStatus("job1"), JobStatus::CANCELLED);
    EXPECT_TRUE(cancelled);
}

TEST_F(JobManagerTest, CleansUpFinishedThreads) {
    manager.SetMaxConcurrentJobs(2);
    
    for (int i = 0; i < 10; ++i) {
        manager.StartJob("job-" + std::to_string(i), "req", [](const std::atomic<bool>*) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        });
        
        // Wait for it to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Starting another job should trigger CleanupFinishedThreads internally
    manager.StartJob("last-job", "req", [](const std::atomic<bool>*) {});
    
    // We can't directly check private threads_ size without exposing it, 
    // but if it didn't throw, it means concurrency limit was respected after cleanup.
}