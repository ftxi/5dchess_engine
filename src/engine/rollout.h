#ifndef ROLLOUT_H
#define ROLLOUT_H

#include <optional>
#include <random>
#include <stop_token>

#include "state.h"

// Returns the winning color (0 for white, 1 for black), or nullopt if
// the rollout ends without a winner.
std::optional<bool> rollout(
    state s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr);

#endif
