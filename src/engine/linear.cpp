#include "linear.h"

#include <algorithm>
#include <array>
#include <chrono>
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

linear_engine::weight_vector_t linear_engine::default_weights()
{
    weight_vector_t weights{};

    // Hand-written priors for features whose direction is unambiguous.  Values
    // are deliberately small because the final score is passed through tanh.
    // Material on a timeline the player must advance is more immediately
    // useful than material on an optional or currently unplayable timeline.
    // Sliding pieces occupy one or more movement-component bitboards, so their
    // effective values are additive.  For example, Princess is LROOK+LBISHOP
    // (0.40), while Queen receives all four sliding components plus its bonus.
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
        weights[optional_material_diff_offset + piece] = 0.5f * material_weights[piece];
        weights[unplayable_material_diff_offset + piece] = 0.15f * material_weights[piece];
    }

    // Positive timeline_advantage means the opponent created more timelines,
    // giving the player to move a larger active-timeline allowance.
    weights[timeline_advantage_offset] = 0.25f;

    // Move-space features are logarithmic, so small coefficients reward
    // flexibility without overwhelming material and timeline advantages.
    weights[log_universe_volume_offset] = 0.04f;
    weights[log_non_new_volume_offset] = 0.04f;

    // Each difference is friendly minus hostile.  Rewarding it values attacks
    // on the opposing royal while penalizing equivalent danger to our own.
    for(std::size_t direction = 0;
        direction < royal_safety_data::EXPOSURE_COUNT;
        ++direction)
    {
        weights[royal_safety_offset + 2 * direction + 1] = 0.08f;
    }
    weights[checks_diff_offset] = 0.25f;
    weights[strong_checks_diff_offset] = 0.40f;
    return weights;
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

    const move_count_data move_counts = count_move_space(position);
    features[log_universe_volume_offset] = move_counts.log_universe_volume;
    features[log_non_new_volume_offset] = move_counts.log_non_new_volume;

    const royal_safety_data royal_safety = count_royal_safety(position);
    for(std::size_t i = 0; i < royal_safety_data::EXPOSURE_COUNT; ++i)
    {
        const float friendly = static_cast<float>(royal_safety.friendly_exposure[i]);
        const float hostile = static_cast<float>(royal_safety.hostile_exposure[i]);
        features[royal_safety_offset + 2 * i] = std::log1p(friendly + hostile);
        features[royal_safety_offset + 2 * i + 1]
            = std::log1p(friendly) - std::log1p(hostile);
    }
    features[checks_sum_offset]
        = static_cast<float>(royal_safety.friendly_checks + royal_safety.hostile_checks);
    features[checks_diff_offset]
        = static_cast<float>(royal_safety.friendly_checks - royal_safety.hostile_checks);
    features[strong_checks_sum_offset]
        = static_cast<float>(royal_safety.friendly_strong_checks
                           + royal_safety.hostile_strong_checks);
    features[strong_checks_diff_offset]
        = static_cast<float>(royal_safety.friendly_strong_checks
                           - royal_safety.hostile_strong_checks);
    return features;
}

float linear_engine::evaluate(const state &position) const
{
    const feature_vector_t features = extract_features(position);
    const double player_score = WINNING_SCORE * predict_player_score(
        features, weight_vector);
    return static_cast<float>(
        position.get_present().second ? -player_score : player_score);
}

double linear_engine::predict_player_score(
    const feature_vector_t &features,
    const weight_vector_t &weights)
{
    const double linear_score = std::inner_product(
        features.begin(),
        features.end(),
        weights.begin(),
        0.0);
    return std::tanh(linear_score);
}

default_policy_result linear_engine::default_policy(
    state position,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    const auto rollout_started = std::chrono::steady_clock::now();
    const rollout_result result = rollout_inplace_detailed(
        position,
        rollout_max_actions.load(),
        stop_token,
        rng);
    const double rollout_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - rollout_started).count();
    if(stop_token.stop_requested())
    {
        return {
            0.0f,
            rollout_termination::STOPPED,
            std::nullopt,
            result.actions,
            rollout_seconds,
            0.0
        };
    }
    if(result.winner.has_value())
    {
        return {
            *result.winner ? -WINNING_SCORE : WINNING_SCORE,
            result.termination,
            result.winner,
            result.actions,
            rollout_seconds,
            0.0
        };
    }
    if(result.termination == rollout_termination::STALEMATE)
    {
        return {
            0.0f,
            result.termination,
            result.winner,
            result.actions,
            rollout_seconds,
            0.0
        };
    }
    const auto evaluation_started = std::chrono::steady_clock::now();
    const float score = evaluate(position);
    const double evaluation_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - evaluation_started).count();
    return {
        score,
        result.termination,
        result.winner,
        result.actions,
        rollout_seconds,
        evaluation_seconds
    };
}
