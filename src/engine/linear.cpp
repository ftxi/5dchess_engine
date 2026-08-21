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

static_assert(linear_engine::features_count == 64);

linear_engine::weight_vector_t linear_engine::default_weights()
{
    weight_vector_t weights{};
    constexpr std::array<float, material_feature_count> material_weights{
        0.05f, // pawn / brawn
        0.15f, // knight
        0.25f, // rook movement component
        0.15f, // bishop movement component
        0.08f, // unicorn movement component
        0.05f, // dragon movement component
        0.45f, // non-royal queen bonus
        0.30f  // royal piece
    };
    for(std::size_t piece = 0; piece < material_weights.size(); ++piece)
    {
        weights[mandatory_material_diff_offset + piece] = material_weights[piece];
        weights[optional_material_diff_offset + piece]
            = 0.5f * material_weights[piece];
        weights[unplayable_material_diff_offset + piece]
            = 0.15f * material_weights[piece];
    }

    weights[timeline_advantage_offset] = 0.25f;
    weights[log_universe_volume_offset] = 0.04f;
    weights[log_non_new_volume_offset] = 0.04f;
    return weights;
}

linear_engine::weight_vector_t linear_engine::trained_weights()
{
    // Projection of the frozen r=40 checkpoint onto the retained 64-feature
    // schema.  The removed royal-safety coefficients are deliberately absent.
    return {
        -0.00123027945f, -0.00600768765f,  0.00338658318f,  0.00632669684f,
        -0.00422760518f, -0.00573036913f, -0.00573036913f, -0.00573036913f,
        -0.0115035931f,   0.000595524965f, 0.0781893134f,   0.0359656215f,
         0.175541759f,   -0.0855880827f,  -0.115588054f,    0.284411639f,
         0.288696021f,   -0.00817218516f, -0.00686811749f, -0.0119211152f,
        -0.0104583567f,  -0.0165845193f,  -0.0165845193f,  -0.0165845193f,
        -0.00623198925f,  0.0197074413f,   0.0567287579f,   0.137343943f,
         0.0912609622f,   0.046411965f,    0.0314119607f,   0.231411979f,
         0.171710014f,    0.0163448583f,   0.0168772414f,   0.015377258f,
         0.0141730411f,   0.0118304137f,   0.0118304137f,   0.0118304137f,
         0.0140779829f,   0.027899893f,    0.0103068734f,   0.0221800804f,
         0.00934366416f, -0.00264071906f, -0.00714072073f,  0.0528592542f,
         0.0637308881f,  -0.0115995659f,  -0.0115995659f,   0.0f,
        -0.0118690757f,  -0.00865917187f,  0.0151735367f,  -0.0135565149f,
         0.0168984998f,   0.064030014f,    0.222902104f,    0.00271896203f,
         0.0168984998f,   0.064030014f,    0.0140336938f,   0.0197210461f
    };
}

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

    const move_space_data move_space = count_move_space(position);
    features[log_universe_volume_offset] = move_space.log_universe_volume;
    features[log_non_new_volume_offset] = move_space.log_non_new_volume;
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

default_policy_result linear_engine::default_policy(
    state position,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    const rollout_result result = rollout_inplace_detailed(
        position,
        rollout_max_actions.load(),
        stop_token,
        rng);
    if(stop_token.stop_requested())
    {
        return {0.0f, rollout_termination::STOPPED};
    }
    if(result.winner.has_value())
    {
        return {
            *result.winner ? -WINNING_SCORE : WINNING_SCORE,
            result.termination
        };
    }
    if(result.termination == rollout_termination::STALEMATE)
    {
        return {0.0f, result.termination};
    }
    return {evaluate(position), result.termination};
}
