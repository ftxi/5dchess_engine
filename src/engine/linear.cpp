#include "linear.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace
{

template<timelines_status Status>
void append_material_features(
    linear_cutoff_evaluation::feature_vector_t &features,
    std::size_t sum_offset,
    std::size_t diff_offset,
    const state &position)
{
    const auto sum = count_material_sum<Status>(position);
    auto diff = count_material_diff<Status>(position);
    // The statistics helper is endpoint-relative. Unplayable endpoints always
    // belong to the opponent, so convert their differences to the stored mover.
    if constexpr(Status == timelines_status::UNPLAYABLE)
        for(auto &value : diff.values) value = -value;
    std::copy(sum.values.begin(), sum.values.end(), features.begin() + sum_offset);
    std::copy(diff.values.begin(), diff.values.end(), features.begin() + diff_offset);
}

bitboard_t royal_side(const board &b, bool color)
{
    return (color ? b.black() : b.white()) & ~b.wall();
}

float ring_pressure(const board &b, int square, bool defender)
{
    int pressure = 0;
    const auto hostile = royal_side(b, !defender);
    const auto support = royal_side(b, defender) & ~b.royal();
    // An enemy cannot land on a square already occupied by its own piece.
    for(int q : marked_pos(king_attack(square) & ~hostile & ~b.wall()))
    {
        const auto attackers = b.attacks_to(q);
        pressure += std::max(0, std::popcount(attackers & hostile)
                                - std::popcount(attackers & support));
    }
    return std::min(1.0f, pressure / 4.0f);
}

float escape_scarcity(const board &b, int square, bool defender)
{
    if(!(b.king() & pmask(square))) return 0.0f;
    int escapes = 0;
    const auto destinations = king_attack(square) & ~royal_side(b, defender)
        & ~b.wall() & ~b.royal();
    for(int q : marked_pos(destinations))
    {
        board result = b;
        result.set_piece(square, NO_PIECE);
        result.set_piece(q, b.get_piece(square));
        escapes += !result.is_under_attack(q, defender);
    }
    return std::max(0, 2 - escapes) / 2.0f;
}

std::array<float, linear_cutoff_evaluation::ROYAL_SAFETY_COUNT>
royal_safety_features(const state &position)
{
    using evaluation = linear_cutoff_evaluation;
    // Keep the worst royal for each side; safe royals cannot average away danger.
    std::array<std::array<float, evaluation::ROYAL_SAFETY_COUNT>, 2> danger{};
    const auto [first, last] = position.get_lines_range();
    const auto [active_first, active_last] = position.get_active_range();
    for(int l = first; l <= last; ++l)
    {
        const auto [t, c] = position.get_timeline_end(l).get();
        const board &b = *position.get_board_ptr(l, t, c);
        const bool active = active_first <= l && l <= active_last;
        for(bool defender : {false, true})
        {
            auto &worst = danger[defender];
            for(int square : marked_pos(b.royal() & royal_side(b, defender)))
            {
                const float pressure = ring_pressure(b, square, defender);
                const float latent = has_latent_king_threat(b, square, defender);
                const auto pressure_index = active ? evaluation::ACTIVE_RING_PRESSURE
                                                   : evaluation::INACTIVE_RING_PRESSURE;
                const auto escape_index = active ? evaluation::ACTIVE_ESCAPE_SCARCITY
                                                 : evaluation::INACTIVE_ESCAPE_SCARCITY;
                worst[pressure_index] = std::max(worst[pressure_index], pressure);
                // Immobility behind a safe shield is not an escape deficit.
                if(pressure > 0.0f || latent > 0.0f || b.is_under_attack(square, defender))
                    worst[escape_index] = std::max(worst[escape_index],
                        escape_scarcity(b, square, defender));
                worst[evaluation::LATENT_ROYAL_THREAT] = std::max(
                    worst[evaluation::LATENT_ROYAL_THREAT], latent);

                const board *previous = t > 0
                    ? position.get_board_ptr(l, t - 1, c) : nullptr;
                if(!previous || (previous->royal() & royal_side(*previous, defender)
                                 & pmask(square))) continue;
                int vulnerable = 0;
                const auto hostile = royal_side(*previous, !defender);
                const auto support = royal_side(*previous, defender) & ~previous->royal();
                for(int q : marked_pos(king_attack(square) & ~hostile & ~previous->wall()))
                {
                    const auto attackers = previous->attacks_to(q);
                    const int attack = std::popcount(attackers & hostile);
                    vulnerable += attack > 0 && attack >= std::popcount(attackers & support);
                }
                worst[evaluation::HISTORICAL_VULNERABILITY] = std::max(
                    worst[evaluation::HISTORICAL_VULNERABILITY],
                    std::min(1.0f, vulnerable / 3.0f));
            }
        }
    }
    std::array<float, evaluation::ROYAL_SAFETY_COUNT> result{};
    const bool player = position.get_present().c();
    for(std::size_t i = 0; i < result.size(); ++i)
        result[i] = danger[!player][i] - danger[player][i];
    return result;
}

} /* anonymous namespace */

static_assert(linear_cutoff_evaluation::royal_safety_offset == 64);
static_assert(linear_cutoff_evaluation::features_count == 70);

linear_cutoff_evaluation::weight_vector_t linear_cutoff_evaluation::default_weights()
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

linear_cutoff_evaluation::weight_vector_t linear_cutoff_evaluation::trained_weights()
{
    // Projection of the frozen r=40 checkpoint onto the retained 64-feature
    // schema.  The removed royal-safety coefficients are deliberately absent.
    weight_vector_t weights{
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
    // Preserve the frozen model's predictions after converting unplayable
    // material from endpoint-relative to player-relative features.
    for(std::size_t i = 0; i < material_feature_count; ++i)
        weights[unplayable_material_diff_offset + i] *= -1.0f;
    return weights;
}

linear_cutoff_evaluation::weight_vector_t linear_cutoff_evaluation::safety_weights()
{
    auto weights = default_weights();
    constexpr std::array<float, ROYAL_SAFETY_COUNT> safety{
        0.35f, 0.15f, 0.25f, 0.10f, 0.20f, 0.25f
    };
    std::copy(safety.begin(), safety.end(), weights.begin() + royal_safety_offset);
    return weights;
}

linear_cutoff_evaluation::weight_vector_t linear_cutoff_evaluation::select_features(
    weight_vector_t weights, std::string_view subset)
{
    if(subset == "all") return weights;
    const bool material = subset == "material" || subset == "material-timeline"
        || subset == "material-volume" || subset == "material-safety";
    const bool mandatory_only = subset == "mandatory-material";
    const bool timeline = subset == "timeline" || subset == "material-timeline"
        || subset == "timeline-volume";
    const bool volume = subset == "volume" || subset == "material-volume"
        || subset == "timeline-volume";
    const bool safety = subset == "safety" || subset == "material-safety";
    if(!material && !mandatory_only && !timeline && !volume && !safety && subset != "none")
        throw std::invalid_argument("unknown evaluation feature subset");

    weight_vector_t selected{};
    auto retain = [&](std::size_t offset, std::size_t count) {
        std::copy_n(weights.begin() + offset, count, selected.begin() + offset);
    };
    if(material || mandatory_only)
        retain(mandatory_material_diff_offset, material_feature_count);
    if(material)
    {
        retain(optional_material_diff_offset, material_feature_count);
        retain(unplayable_material_diff_offset, material_feature_count);
    }
    if(timeline) retain(timeline_advantage_offset, 1);
    if(volume) retain(move_space_offset, move_space_feature_count);
    if(safety) retain(royal_safety_offset, ROYAL_SAFETY_COUNT);
    return selected;
}

linear_cutoff_evaluation::feature_vector_t linear_cutoff_evaluation::extract_features(
    const state &position)
{
    return extract_features(position, count_move_space(position));
}

linear_cutoff_evaluation::feature_vector_t linear_cutoff_evaluation::extract_features(
    const state &position, const move_space_data &move_space)
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

    features[log_universe_volume_offset] = move_space.log_universe_volume;
    features[log_non_new_volume_offset] = move_space.log_non_new_volume;
    const auto safety = royal_safety_features(position);
    std::copy(safety.begin(), safety.end(), features.begin() + royal_safety_offset);
    return features;
}

float linear_cutoff_evaluation::raw_score(const state &position) const
{
    const feature_vector_t features = extract_features(position);
    return std::inner_product(
        features.begin(),
        features.end(),
        weights.begin(),
        0.0f);
}

float linear_cutoff_evaluation::evaluate(const state &position) const
{
    const float player_score = WINNING_SCORE * std::tanh(raw_score(position));
    return position.get_present().c() ? -player_score : player_score;
}

linear_cutoff_evaluation::result_type linear_cutoff_evaluation::mate_reward(
    std::optional<bool> winner,
    std::size_t length) const
{
    return rollout_cutoff_evaluation{}.mate_reward(winner, length);
}

std::optional<linear_cutoff_evaluation::result_type>
linear_cutoff_evaluation::cutoff_reward(
    state position,
    std::size_t length,
    std::stop_token stop_token,
    std::mt19937 *) const
{
    if(stop_token.stop_requested()) return std::nullopt;
    return result_type{
        evaluate(position),
        {rollout_details::termination::ACTION_LIMIT, length}};
}
