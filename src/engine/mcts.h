#ifndef MCTS_H
#define MCTS_H

#include <cstddef>
#include <optional>
#include <mutex>
#include <atomic>
#include <stop_token>
#include <memory>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
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
    bool player; // whose turn it is at this node (true=white, false=black)
    mcts_node_info() : is_included{false}, all_children_included{false}, fully_expanded{false}, sum_reward{0.0f}, visits{0}, player{false} {}
};

struct fine_tree_search_diagnostic
{
    std::string operation;
    double seconds;
    state nodal_state;
    std::vector<std::pair<index_t, index_t>> path;
    std::vector<index_t> searched_children;
};

class mcts_engine : public engine
{
    std::unique_ptr<fine_node<mcts_node_info>> root;
    fine_tree_options fine_tree_config;
    std::optional<std::uint32_t> rollout_seed;
    std::optional<double> diagnostic_threshold;
    std::vector<fine_tree_search_diagnostic> search_diagnostics;
public:
    mcts_engine(
        std::unique_ptr<io_handler> io_handler,
        fine_tree_options options = {},
        std::optional<std::uint32_t> seed = std::nullopt,
        std::optional<double> threshold = std::nullopt)
    : engine(std::move(io_handler)),
      root(nullptr),
      fine_tree_config(options),
      rollout_seed(seed),
      diagnostic_threshold(threshold),
      search_diagnostics() {}
    void initialize() override;
    std::optional<action> find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token) override;
    const std::vector<fine_tree_search_diagnostic> &
    get_search_diagnostics() const { return search_diagnostics; }
};

#endif /* MCTS_H */
