#include <gtest/gtest.h>
#include "job_manager.h"
#include <thread>
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

    manager.StartJob("job1", "req1", [&]() {
        std::unique_lock<std::mutex> lock(mtx);
        ready = true;
        cv.notify_one();
        cv.wait(lock, [&]() { return proceed; });
    });

    // Wait for job1 to start
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return ready; });
    }

    // Try to start job2, should fail
    EXPECT_THROW({
        manager.StartJob("job2", "req2", []() {});
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

    manager.StartJob("job1", "req1", []() {
        // Quick job
    });

    // Wait slightly for job1 to finish (polling since we don't have a better signal exposed yet)
    int retries = 100;
    while (retries-- > 0) {
        if (manager.GetStatus("job1") == JobStatus::COMPLETED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(manager.GetStatus("job1"), JobStatus::COMPLETED);

    // Should succeed now
    EXPECT_NO_THROW({
        manager.StartJob("job2", "req2", []() {});
    });
}