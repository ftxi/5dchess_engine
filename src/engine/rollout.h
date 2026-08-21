#ifndef ROLLOUT_H
#define ROLLOUT_H

#include <cstddef>
#include <optional>
#include <random>
#include <stop_token>

#include "state.h"

enum class rollout_termination
{
    WINNER,
    STALEMATE,
    ACTION_LIMIT,
    STOPPED
};

struct rollout_result
{
    rollout_termination termination;
    std::optional<bool> winner;
    std::size_t actions;

    constexpr bool is_conclusive() const
    {
        return termination == rollout_termination::WINNER
            || termination == rollout_termination::STALEMATE;
    }
};

// Reports why the rollout ended as well as the winning color, if any.
rollout_result rollout_inplace_detailed(
    state &s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr);

rollout_result rollout_detailed(
    state s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr);

// Returns the winning color (0 for white, 1 for black), or nullopt if
// the rollout ends without a winner. Prefer the detailed API when the caller
// needs to distinguish stalemate, action-limit, and stopped rollouts.
std::optional<bool> rollout_inplace(
    state &s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr);

// Runs a rollout on a private copy of the supplied state.
std::optional<bool> rollout(
    state s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr);

#endif
