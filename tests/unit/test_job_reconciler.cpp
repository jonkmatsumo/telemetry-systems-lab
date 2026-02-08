#include <gtest/gtest.h>
#include "job_reconciler.h"
#include "mocks/mock_db_client.h"
#include <thread>

namespace {

using namespace telemetry;
using namespace testing;

class JobReconcilerTest : public Test {
protected:
    std::shared_ptr<MockDbClient> mock_db = std::make_shared<MockDbClient>();
};

TEST_F(JobReconcilerTest, StartupReconciliationCallsDb) {
    EXPECT_CALL(*mock_db, ReconcileStaleJobs(Eq(std::nullopt))).Times(1);
    
    JobReconciler reconciler(mock_db);
    reconciler.ReconcileStartup();
}

TEST_F(JobReconcilerTest, PeriodicSweepCallsDb) {
    std::chrono::seconds ttl(5);
    // We expect at least one call to ReconcileStaleJobs with the TTL
    EXPECT_CALL(*mock_db, ReconcileStaleJobs(Optional(ttl))).Times(AtLeast(1));
    
    JobReconciler reconciler(mock_db, ttl);
    reconciler.Start(std::chrono::milliseconds(100));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    reconciler.Stop();
}

} // namespace
