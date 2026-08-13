#ifndef MCTS_H
#define MCTS_H

#include <cstddef>
#include <optional>
#include <mutex>
#include <atomic>
#include <stop_token>
#include <memory>
#include <cstdint>
#include "uci.h"
#include "finetree.h"

// Default exploration constant (sqrt(2)) for the UCT selection policy
constexpr float exploration_constant = 1.4142135623730951f;
constexpr int default_mcts_rollout_max_actions = 200;

// Adversarial UCT score using rewards from White's fixed perspective.
// White maximizes average_reward + exploration; Black minimizes
// average_reward - exploration.
float uct(
    float sum_reward,
    std::size_t visits,
    std::size_t parent_visits,
    bool maximizing_player
);

struct mcts_node_info
{
    bool is_included; // is this node inside the mcts tree?
    bool all_children_included; // are all children of this node included in the mcts tree?
    bool fully_expanded; // are all children of this node expanded?
    float sum_reward;
    std::size_t visits;
    mcts_node_info()
    : is_included{false},
      all_children_included{false},
      fully_expanded{false},
      sum_reward{0.0f},
      visits{0} {}
    mcts_node_info(const mcts_node_info&) = delete;
    mcts_node_info &operator=(const mcts_node_info&) = delete;
    mcts_node_info(mcts_node_info&&) noexcept = default;
    mcts_node_info &operator=(mcts_node_info&&) noexcept = default;
};

class mcts_engine : public engine
{
    std::unique_ptr<fine_node<mcts_node_info>> root;
    std::optional<std::uint32_t> rollout_seed;
    std::atomic<int> rollout_max_actions;
protected:
    void on_option_changed(const std::string &key, const option_value_t &value) override;
public:
    mcts_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_mcts_rollout_max_actions)
    : engine(std::move(io_handler)),
      root(nullptr),
      rollout_seed(seed),
      rollout_max_actions(max_rollout_actions) {}
    void initialize() override;
    std::optional<action> find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token) override;
};

#endif /* MCTS_H */
