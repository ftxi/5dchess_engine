#ifndef LINEAR_H
#define LINEAR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <stop_token>

#include "mcts.h"
#include "statistics.h"

class linear_engine : public mcts_engine
{
public:
    static constexpr std::size_t material_feature_count
        = material_data<int>::COUNT;
    static constexpr std::size_t timeline_feature_count
        = timeline_data::COUNT;

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
    static constexpr std::size_t features_count
        = timeline_offset + timeline_feature_count;

    using feature_vector_t = std::array<float, features_count>;
    using weight_vector_t = feature_vector_t;

private:
    weight_vector_t weight_vector{};

protected:
    float default_policy(
        state position,
        std::stop_token stop_token,
        std::mt19937 *rng) override;

public:
    linear_engine(
        std::unique_ptr<io_handler> io_handler,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_rollout_actions = default_mcts_rollout_max_actions,
        weight_vector_t weights = {})
    : mcts_engine(std::move(io_handler), seed, max_rollout_actions),
      weight_vector(std::move(weights)) {}

    static feature_vector_t extract_features(const state &position);
    float evaluate(const state &position) const;

    const weight_vector_t &get_weights() const
    {
        return weight_vector;
    }
};

#endif /* LINEAR_H */
