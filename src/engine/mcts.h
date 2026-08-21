#ifndef MCTS_H
#define MCTS_H

#include <cstddef>
#include <optional>
#include <mutex>
#include <atomic>
#include <stop_token>
#include <memory>
#include <random>
#include <cstdint>
#include "uci.h"
#include "finetree.h"
#include "rollout.h"
#include "uct.h"

constexpr int default_mcts_rollout_max_actions = 200;

constexpr float WINNING_SCORE = 1.0f;

struct default_policy_result
{
    float score;
    rollout_termination termination;
};

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
protected:
    std::unique_ptr<fine_node<mcts_node_info>> root;
    std::optional<std::uint32_t> rollout_seed;
    std::atomic<int> rollout_max_actions;
    void on_option_changed(const std::string &key, const option_value_t &value) override;
    virtual default_policy_result default_policy(
        state position,
        std::stop_token stop_token,
        std::mt19937 *rng);
public:
    mcts_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_mcts_rollout_max_actions
    ) : engine(std::move(io_handler)),
      root(nullptr),
      rollout_seed(seed),
      rollout_max_actions(max_rollout_actions) {}
    void initialize() override;
    std::optional<action> find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token) override;
};

class zero_engine : public mcts_engine
{
public:
    using mcts_engine::mcts_engine;
    default_policy_result default_policy(
        state,
        std::stop_token,
        std::mt19937 *) override
    {
        return {0.0f, rollout_termination::ACTION_LIMIT};
    }
};

#endif /* MCTS_H */
