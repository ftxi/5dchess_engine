#ifndef FLAT_UCB_H
#define FLAT_UCB_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <vector>

#include "rollout_policy.h"

constexpr int default_flat_ucb_rollout_max_actions = 200;

class flat_ucb_engine : public engine
{
    rollout_default_policy default_policy;

protected:
    void on_option_changed(const std::string &key, const option_value_t &value) override;

public:
    flat_ucb_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_flat_ucb_rollout_max_actions)
    : engine(std::move(io_handler)),
      default_policy(
          random_action_selection{},
          rollout_cutoff_evaluation{},
          static_cast<std::size_t>(std::max(0, max_rollout_actions)),
          seed) {}

    void initialize() override {}
    std::optional<action> find_best_move(
        std::optional<int> depth_limit,
        std::optional<int> time_limit_ms,
        std::stop_token stop_token) override;
};

#endif // FLAT_UCB_H
