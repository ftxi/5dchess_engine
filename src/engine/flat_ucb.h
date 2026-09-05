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

template<class DefaultPolicy>
class basic_flat_ucb_engine : public engine
{
    DefaultPolicy default_policy;

protected:
    void on_option_changed(const std::string &key, const option_value_t &value) override;

public:
    basic_flat_ucb_engine(
        std::unique_ptr<io_handler> io_handler,
        DefaultPolicy default_policy)
    : engine(std::move(io_handler)),
      default_policy(std::move(default_policy)) {}

    void initialize() override {}
    std::optional<action> find_best_move(
        std::optional<int> depth_limit,
        std::optional<int> time_limit_ms,
        std::stop_token stop_token) override;
};

extern template class basic_flat_ucb_engine<rollout_default_policy>;
extern template class basic_flat_ucb_engine<weighted_rollout_default_policy>;

class flat_ucb_engine final
    : public basic_flat_ucb_engine<rollout_default_policy>
{
public:
    flat_ucb_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_flat_ucb_rollout_max_actions)
        : basic_flat_ucb_engine(
            std::move(io_handler),
            rollout_default_policy{
                random_action_selection{}, rollout_cutoff_evaluation{},
                static_cast<std::size_t>(std::max(0, max_rollout_actions)),
                seed}) {}
};

class flat_ucb_weighted_engine final
    : public basic_flat_ucb_engine<weighted_rollout_default_policy>
{
public:
    flat_ucb_weighted_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_flat_ucb_rollout_max_actions,
        float weight_temperature = default_move_info_temperature)
        : basic_flat_ucb_engine(
            std::move(io_handler),
            weighted_rollout_default_policy{
                weighted_action_selection{
                    default_move_info_weights, weight_temperature},
                rollout_cutoff_evaluation{},
                static_cast<std::size_t>(std::max(0, max_rollout_actions)),
                seed}) {}
};

#endif // FLAT_UCB_H
