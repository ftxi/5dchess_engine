#include "linear.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

#include "rollout.h"

namespace
{

template<timelines_status Status>
void append_material_features(
    linear_engine::feature_vector_t &features,
    std::size_t sum_offset,
    std::size_t diff_offset,
    const state &position)
{
    const auto sum = count_material_sum<Status>(position);
    const auto diff = count_material_diff<Status>(position);
    std::copy(sum.values.begin(), sum.values.end(), features.begin() + sum_offset);
    std::copy(diff.values.begin(), diff.values.end(), features.begin() + diff_offset);
}

} /* anonymous namespace */

linear_engine::feature_vector_t linear_engine::extract_features(
    const state &position)
{
    feature_vector_t features{};
    features[bias_offset] = 1.0f;

    append_material_features<timelines_status::MANDATORY>(
        features,
        mandatory_material_sum_offset,
        mandatory_material_diff_offset,
        position);
    append_material_features<timelines_status::OPTIONAL>(
        features,
        optional_material_sum_offset,
        optional_material_diff_offset,
        position);
    append_material_features<timelines_status::UNPLAYABLE>(
        features,
        unplayable_material_sum_offset,
        unplayable_material_diff_offset,
        position);

    const timeline_data timelines = count_timelines(position);
    const std::array<int, timeline_feature_count> timeline_features{
        timelines.total,
        timelines.active,
        timelines.inactive,
        timelines.mandatory,
        timelines.optional,
        timelines.unplayable,
        timelines.playable,
        timelines.friendly_created,
        timelines.hostile_created,
        timelines.timeline_advantage,
        timelines.active_timeline_allowance,
        timelines.friendly_active_created,
        timelines.hostile_active_created
    };
    std::copy(
        timeline_features.begin(),
        timeline_features.end(),
        features.begin() + timeline_offset);
    return features;
}

float linear_engine::evaluate(const state &position) const
{
    const feature_vector_t features = extract_features(position);
    const float linear_score = std::inner_product(
        features.begin(),
        features.end(),
        weight_vector.begin(),
        0.0f);
    const float player_score = WINNING_SCORE * std::tanh(linear_score);
    return position.get_present().second ? -player_score : player_score;
}

float linear_engine::default_policy(
    state position,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    const std::optional<bool> winner = rollout_inplace(
        position,
        rollout_max_actions.load(),
        stop_token,
        rng);
    if(stop_token.stop_requested())
    {
        return 0.0f;
    }
    if(winner.has_value())
    {
        return *winner ? -WINNING_SCORE : WINNING_SCORE;
    }
    return evaluate(position);
}
