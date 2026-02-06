#include <gtest/gtest.h>
#include "job_state_machine.h"

namespace {

using namespace telemetry::job;

TEST(JobStateMachineTest, ValidTransitions) {
    // PENDING -> RUNNING (Start)
    EXPECT_TRUE(JobStateMachine::IsTransitionAllowed(JobState::PENDING, JobState::RUNNING));
    
    // RUNNING -> COMPLETED (Success)
    EXPECT_TRUE(JobStateMachine::IsTransitionAllowed(JobState::RUNNING, JobState::COMPLETED));
    
    // RUNNING -> FAILED (Error)
    EXPECT_TRUE(JobStateMachine::IsTransitionAllowed(JobState::RUNNING, JobState::FAILED));
    
    // RUNNING -> CANCELLED (User request)
    EXPECT_TRUE(JobStateMachine::IsTransitionAllowed(JobState::RUNNING, JobState::CANCELLED));
    
    // PENDING -> CANCELLED (Pre-start cancellation)
    EXPECT_TRUE(JobStateMachine::IsTransitionAllowed(JobState::PENDING, JobState::CANCELLED));
    
    // Self-transition (Idempotency)
    EXPECT_TRUE(JobStateMachine::IsTransitionAllowed(JobState::RUNNING, JobState::RUNNING));
}

TEST(JobStateMachineTest, InvalidTransitions) {
    // COMPLETED is terminal
    EXPECT_FALSE(JobStateMachine::IsTransitionAllowed(JobState::COMPLETED, JobState::RUNNING));
    EXPECT_FALSE(JobStateMachine::IsTransitionAllowed(JobState::COMPLETED, JobState::FAILED));
    
    // FAILED is terminal
    EXPECT_FALSE(JobStateMachine::IsTransitionAllowed(JobState::FAILED, JobState::RUNNING));
    
    // CANCELLED is terminal
    EXPECT_FALSE(JobStateMachine::IsTransitionAllowed(JobState::CANCELLED, JobState::RUNNING));
}

TEST(JobStateMachineTest, TerminalStates) {
    EXPECT_TRUE(JobStateMachine::IsTerminal(JobState::COMPLETED));
    EXPECT_TRUE(JobStateMachine::IsTerminal(JobState::FAILED));
    EXPECT_TRUE(JobStateMachine::IsTerminal(JobState::CANCELLED));
    EXPECT_FALSE(JobStateMachine::IsTerminal(JobState::PENDING));
    EXPECT_FALSE(JobStateMachine::IsTerminal(JobState::RUNNING));
}

TEST(JobStateMachineTest, StringConversions) {
    EXPECT_EQ(StateToString(JobState::PENDING), "PENDING");
    EXPECT_EQ(StringToState("RUNNING"), JobState::RUNNING);
}

} // namespace
