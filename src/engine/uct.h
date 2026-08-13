#ifndef UCT_H
#define UCT_H

#include <cstddef>

constexpr float exploration_constant = 1.4142135623730951f;

// Adversarial UCT score using rewards from White's fixed perspective.
// White maximizes average_reward + exploration; Black minimizes
// average_reward - exploration.
float uct(
    float sum_reward,
    std::size_t visits,
    std::size_t parent_visits,
    bool maximizing_player);

#endif // UCT_H
