#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <set>

namespace telemetry::job {

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

auto StateToString(JobState state) -> std::string;
auto StringToState(const std::string& state_str) -> JobState;

/**
 * @brief Manages valid state transitions for jobs.
 */
class JobStateMachine {
public:
    /**
     * @brief Checks if a transition from current_state to next_state is allowed.
     */
    static auto IsTransitionAllowed(JobState current, JobState next) -> bool;

    /**
     * @brief Returns the set of valid next states from the current state.
     */
    static auto GetValidNextStates(JobState current) -> std::set<JobState>;

    /**
     * @brief Checks if a state is terminal (no further transitions allowed).
     */
    static auto IsTerminal(JobState state) -> bool;
};

} // namespace telemetry::job
