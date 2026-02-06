#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <set>

namespace telemetry {
namespace job {

/**
 * @brief Represents the possible states of a job or run.
 */
enum class JobState {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

std::string StateToString(JobState state);
JobState StringToState(const std::string& state_str);

/**
 * @brief Manages valid state transitions for jobs.
 */
class JobStateMachine {
public:
    /**
     * @brief Checks if a transition from current_state to next_state is allowed.
     */
    static bool IsTransitionAllowed(JobState current, JobState next);

    /**
     * @brief Returns the set of valid next states from the current state.
     */
    static std::set<JobState> GetValidNextStates(JobState current);

    /**
     * @brief Checks if a state is terminal (no further transitions allowed).
     */
    static bool IsTerminal(JobState state);
};

} // namespace job
} // namespace telemetry
