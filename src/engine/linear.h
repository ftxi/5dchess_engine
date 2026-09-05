#ifndef LINEAR_H
#define LINEAR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <stop_token>
#include <utility>

#include "mcts_engines.h"
#include "statistics.h"

class linear_cutoff_evaluation
{
public:
    static constexpr std::size_t material_feature_count = material_data<int>::COUNT;
    static constexpr std::size_t timeline_feature_count = timeline_data::COUNT;
    static constexpr std::size_t move_space_feature_count = move_space_data::COUNT;

    static constexpr std::size_t bias_offset = 0;
    static constexpr std::size_t mandatory_material_sum_offset = 1;
    static constexpr std::size_t mandatory_material_diff_offset
        = mandatory_material_sum_offset + material_feature_count;
    static constexpr std::size_t optional_material_sum_offset
        = mandatory_material_diff_offset + material_feature_count;
    static constexpr std::size_t optional_material_diff_offset
        = optional_material_sum_offset + material_feature_count;
    static constexpr std::size_t unplayable_material_sum_offset
        = optional_material_diff_offset + material_feature_count;
    static constexpr std::size_t unplayable_material_diff_offset
        = unplayable_material_sum_offset + material_feature_count;
    static constexpr std::size_t timeline_offset
        = unplayable_material_diff_offset + material_feature_count;
    static constexpr std::size_t move_space_offset
        = timeline_offset + timeline_feature_count;
    static constexpr std::size_t log_universe_volume_offset = move_space_offset;
    static constexpr std::size_t log_non_new_volume_offset = move_space_offset + 1;
    static constexpr std::size_t features_count
        = move_space_offset + move_space_feature_count;
    static constexpr std::size_t timeline_advantage_offset = timeline_offset + 9;

    using feature_vector_t = std::array<float, features_count>;
    using weight_vector_t = feature_vector_t;
    using result_type = reward_t<rollout_details>;

private:
    weight_vector_t weights;

public:
    explicit linear_cutoff_evaluation(weight_vector_t weights)
        : weights(std::move(weights)) {}

    static weight_vector_t default_weights();
    static weight_vector_t trained_weights();
    static feature_vector_t extract_features(const state &position);

    float evaluate(const state &position) const;
    const weight_vector_t &get_weights() const { return weights; }

    result_type mate_reward(std::optional<bool> winner, std::size_t length) const;
    std::optional<result_type> cutoff_reward(
        state position,
        std::size_t length,
        std::stop_token stop_token,
        std::mt19937 *) const;
};

using linear_default_policy = default_policy_t<
    rollout_details,
    random_action_selection,
    linear_cutoff_evaluation>;
using weighted_linear_default_policy = default_policy_t<
    rollout_details,
    weighted_action_selection,
    linear_cutoff_evaluation>;

using linear_mcts_engine = basic_mcts_engine<
    uct_tree_policy,
    linear_default_policy,
    sum_backpropagation,
    most_visited_selection,
    mcts_observer>;
using weighted_linear_mcts_engine = basic_mcts_engine<
    uct_tree_policy,
    weighted_linear_default_policy,
    sum_backpropagation,
    most_visited_selection,
    mcts_observer>;

class linear_engine final : public linear_mcts_engine
{
public:
    linear_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_mcts_rollout_max_actions,
        linear_cutoff_evaluation::weight_vector_t weights
            = linear_cutoff_evaluation::default_weights())
        :   linear_mcts_engine(
                linear_default_policy{
                    random_action_selection{},
                    linear_cutoff_evaluation{std::move(weights)},
                    static_cast<std::size_t>(std::max(0, max_rollout_actions)),
                    seed
                },
                uct_tree_policy{seed}, {}, {}, {}, std::move(io_handler)
            ) {}
};

class linear_weighted_engine final : public weighted_linear_mcts_engine
{
public:
    linear_weighted_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_mcts_rollout_max_actions,
        linear_cutoff_evaluation::weight_vector_t weights
            = linear_cutoff_evaluation::default_weights(),
        float weight_temperature = default_move_info_temperature)
        : weighted_linear_mcts_engine(
            weighted_linear_default_policy{
                weighted_action_selection{
                    default_move_info_weights, weight_temperature},
                linear_cutoff_evaluation{std::move(weights)},
                static_cast<std::size_t>(std::max(0, max_rollout_actions)),
                seed
            },
            uct_tree_policy{seed}, {}, {}, {}, std::move(io_handler)
        ) {}
};

static_assert(CutoffEvaluation<linear_cutoff_evaluation, rollout_details>);
static_assert(DefaultPolicy<linear_default_policy, mcts_observer>);
static_assert(DefaultPolicy<weighted_linear_default_policy, mcts_observer>);

#endif /* LINEAR_H */
