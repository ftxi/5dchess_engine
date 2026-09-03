#ifndef ROLLOUT_H
#define ROLLOUT_H

#include <cstddef>
#include <random>
#include <stop_token>

#include "state.h"

struct rollout_result
{
    enum class termination
    {
        WHITE_WINS,
        BLACK_WINS,
        STALEMATE,
        ACTION_LIMIT,
        STOPPED
    };

    termination end;
    std::size_t num_actions;

    constexpr bool is_conclusive() const
    {
        return end == termination::WHITE_WINS
            || end == termination::BLACK_WINS
            || end == termination::STALEMATE;
    }
};

// Runs a rollout directly on the supplied state and reports why it ended.
rollout_result rollout_inplace(
    state &s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr
);

// Runs a rollout on a private copy of the supplied state.
rollout_result rollout(
    state s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr
);

#endif
