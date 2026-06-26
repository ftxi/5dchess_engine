#ifndef MCTS_H
#define MCTS_H

#include <cstddef>
#include <optional>
#include <mutex>
#include <atomic>
#include <stop_token>
#include <memory>
#include "uci.h"
#include "finetree.h"

// Default exploration constant (sqrt(2)) for the UCT selection policy
constexpr float exploration_constant = 1.4142135623730951f;

// UCT score for a node using cumulative reward and visit counts.
/* returns average_reward + exploration_term
   where average_reward = sum_reward / visits
       exploration_term = exploration_constant * sqrtf(logf(parent_visits) / visits)
*/ 
float uct(float sum_reward, std::size_t visits, std::size_t parent_visits);

struct mcts_node_info
{
    bool is_included; // is this node inside the mcts tree?
    bool all_children_included; // are all children of this node included in the mcts tree?
    bool fully_expanded; // are all children of this node expanded?
    float sum_reward;
    std::size_t visits;
    mcts_node_info() : is_included{false}, all_children_included{false}, fully_expanded{false}, sum_reward{0.0f}, visits{0} {}
};

class mcts_engine : public engine
{
    std::unique_ptr<fine_node<mcts_node_info>> root;
public:
    mcts_engine(std::unique_ptr<io_handler> io_handler)
    : engine(std::move(io_handler)), root(nullptr) {}
    void initialize() override;
    std::optional<action> find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token) override;
};

#endif /* MCTS_H */
