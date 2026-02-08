#include "job_state_machine.h"
#include <stdexcept>

namespace telemetry::job {

auto StateToString(JobState state) -> std::string {
    switch (state) {
        case JobState::PENDING: return "PENDING";
        case JobState::RUNNING: return "RUNNING";
        case JobState::COMPLETED: return "COMPLETED";
        case JobState::FAILED: return "FAILED";
        case JobState::CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

auto StringToState(const std::string& state_str) -> JobState {
    if (state_str == "PENDING") { return JobState::PENDING; }
    if (state_str == "RUNNING") { return JobState::RUNNING; }
    if (state_str == "COMPLETED") { return JobState::COMPLETED; }
    if (state_str == "FAILED") { return JobState::FAILED; }
    if (state_str == "CANCELLED") { return JobState::CANCELLED; }
    throw std::runtime_error("Invalid job state string: " + state_str);
}

auto JobStateMachine::IsTransitionAllowed(JobState current, JobState next) -> bool {
    if (current == next) { return true; }

    switch (current) {
        case JobState::PENDING:
            return next == JobState::RUNNING || next == JobState::CANCELLED || next == JobState::FAILED;
        case JobState::RUNNING:
            return next == JobState::COMPLETED || next == JobState::FAILED || next == JobState::CANCELLED;
        case JobState::COMPLETED:
        case JobState::FAILED:
        case JobState::CANCELLED:
        default:
            return false;
    }
}

auto JobStateMachine::GetValidNextStates(JobState current) -> std::set<JobState> {
    std::set<JobState> next;
    next.insert(current);
    for (int i = 0; i <= (int)JobState::CANCELLED; ++i) {
        auto s = static_cast<JobState>(i);
        if (IsTransitionAllowed(current, s)) {
            next.insert(s);
        }
    }
    return next;
}

auto JobStateMachine::IsTerminal(JobState state) -> bool {
    return state == JobState::COMPLETED || state == JobState::FAILED || state == JobState::CANCELLED;
}

} // namespace telemetry::job
