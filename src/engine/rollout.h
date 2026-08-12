#ifndef ROLLOUT_H
#define ROLLOUT_H

#include <limits>
#include <random>
#include <stop_token>

#include "state.h"

struct simulation_result
{
    float outcome;
    int actions;
    bool limit_reached;
    bool aborted;
};

simulation_result default_policy(
    state s,
    int max_actions,
    std::stop_token stop_token = {},
    std::mt19937 *rng = nullptr,
    float winning_score = std::numeric_limits<float>::infinity());

#endif
