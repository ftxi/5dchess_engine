#include "move_evaluation.h"

#include <algorithm>
#include <array>

#include "hypercuboid.h"

namespace
{
// Only LVA needs ranks. Derive distinct material tiers at compile time;
// royals are the most expensive attackers and never participate as victims.
struct lva_rank_table
{
    std::array<int, 128> ranks{};
    int royal_rank;
    float minimum_value, maximum_value, minimum_gap;
};

consteval lva_rank_table make_lva_ranks()
{
    constexpr std::array pieces{
        QUEEN_W, PRINCESS_W, BISHOP_W, KNIGHT_W, ROOK_W,
        COMMON_KING_W, BRAWN_W, PAWN_W, UNICORN_W, DRAGON_W};
    std::array<float, pieces.size()> values{};
    for(std::size_t i = 0; i < pieces.size(); ++i)
        values[i] = capture_piece_value(pieces[i]);
    std::sort(values.begin(), values.end());
    const auto end = std::unique(values.begin(), values.end());
    const int tier_count = static_cast<int>(end - values.begin());
    float minimum_gap = *(end - 1) - values.front();
    for(auto it = values.begin() + 1; it != end; ++it)
        minimum_gap = std::min(minimum_gap, *it - *(it - 1));

    lva_rank_table result{
        {}, tier_count + 1, values.front(), *(end - 1), minimum_gap};
    for(piece_t piece : pieces)
        result.ranks[piece] = 1 + static_cast<int>(
            std::lower_bound(values.begin(), end, capture_piece_value(piece)) - values.begin());
    result.ranks[KING_W] = result.royal_rank;
    result.ranks[ROYAL_QUEEN_W] = result.royal_rank;
    return result;
}

inline constexpr auto lva_ranks = make_lva_ranks();
inline constexpr float mvvlva_capture_base = 480.0f;
inline constexpr float mvvlva_material_scale = 1.0f / 16.0f;
// The entire LVA bonus is strictly below the smallest scaled victim-value gap.
inline constexpr float mvvlva_lva_scale
    = mvvlva_material_scale * lva_ranks.minimum_gap / lva_ranks.royal_rank;
inline constexpr float solid_material_scale = 1.0f / 4.0f;
inline constexpr float optional_move_penalty = 1024.0f;
inline constexpr float optional_departure_penalty = 480.0f;
inline constexpr float branch_arrival_penalty = 280.0f;
inline constexpr float maximum_exposure_penalty = 128.0f;
inline constexpr float maximum_promotion_bonus = 48.0f;
inline constexpr float exposure_material_scale = 1.0f / 10.0f;
inline constexpr float minimum_capture_score
    = mvvlva_capture_base + mvvlva_material_scale * lva_ranks.minimum_value;
inline constexpr float maximum_capture_score
    = mvvlva_capture_base + mvvlva_material_scale * lva_ranks.maximum_value
        + mvvlva_lva_scale * (lva_ranks.royal_rank - 1);
inline constexpr float check_bonus = capture_check_weights.values[move_evaluation_weights::CHECK];
static_assert(lva_ranks.minimum_value > 0.0f && lva_ranks.minimum_gap > 0.0f);
// Preserve these priority groups for MVV-LVA and MVV-LVA exposure.
static_assert(minimum_capture_score - maximum_exposure_penalty
    > check_bonus + maximum_promotion_bonus);
static_assert(minimum_capture_score - maximum_exposure_penalty + check_bonus
    > maximum_capture_score + maximum_promotion_bonus);

constexpr int lva_rank(piece_t piece)
{
    return lva_ranks.ranks[to_white(piece_name(piece))];
}
} /* anonymous namespace */

move_evaluator::move_evaluator(const HC_info &info,
                               move_evaluation_weights weights,
                               capture_ordering captures)
    : info(info), weights(weights), features(info),
      evaluate_captures(std::any_of(
          weights.values.begin() + move_evaluation_weights::QUEEN_CAPTURE,
          weights.values.begin() + move_evaluation_weights::PAWN_CAPTURE + 1,
          [](float weight) { return weight != 0.0f; })), captures(captures),
      optional_lines(captures == capture_ordering::solid
          ? std::get<1>(info.get_state().get_timeline_status()) : std::vector<int>{})
{}

float move_evaluator::score(index_t axis, index_t coordinate)
{
    const bool solid_ordering = captures == capture_ordering::solid;
    const bool score_exposure = solid_ordering || captures == capture_ordering::mvvlva_exposure;
    const auto optional = [&](int line) {
        return std::find(optional_lines.begin(), optional_lines.end(), line)
            != optional_lines.end();
    };
    const auto boards = info.get_move_boards(axis, coordinate);
    if(!boards)
    {
        if(solid_ordering)
        {
            const auto move = info.get_semimove(axis, coordinate);
            if(const auto *departure = move.get_if<departing_move>();
               departure && optional(departure->from.l()))
                return -optional_departure_penalty;
        }
        return 0.0f;
    }

    using enum move_evaluation_weights::indices;
    const auto &w = weights.values;
    const full_move &move = boards->move;
    float result = 0.0f;

    bool king_move = false;
    if(w[LATENT_KING_THREAT] != 0.0f || w[NORMAL_KING_MOVE] != 0.0f
       || w[KING_BRANCH] != 0.0f)
    {
        const piece_t moved = to_white(piece_name(move.moved_piece(info.get_state())));
        king_move = moved == KING_W || moved == COMMON_KING_W;
    }

    if(king_move && (w[LATENT_KING_THREAT] != 0.0f
                    || w[NORMAL_KING_MOVE] != 0.0f))
    {
        // Threat detection also determines whether the normal king penalty
        // applies, even when the threat's own coefficient is zero.
        result += features.has_latent_king_threat(axis, coordinate)
            ? w[LATENT_KING_THREAT] : w[NORMAL_KING_MOVE];
    }

    special_move_t move_type = special_move_t::NONE;
    if((king_move && w[KING_BRANCH] != 0.0f) || w[SUPERPHYSICAL] != 0.0f)
        move_type = move.move_type(info.get_state());

    if(king_move && w[KING_BRANCH] != 0.0f
       && static_cast<bool>(move_type & special_move_t::BRANCHING))
        result += w[KING_BRANCH];

    if(captures != capture_ordering::weighted)
    {
        const piece_t victim = to_white(piece_name(
            move.captured_piece(info.get_state())));
        const float victim_value = capture_piece_value(victim);
        if(victim_value > 0.0f && victim != KING_W && victim != ROYAL_QUEEN_W)
        {
            const float material_scale = solid_ordering ? solid_material_scale : mvvlva_material_scale;
            float capture_score = material_scale * victim_value
                + mvvlva_lva_scale * (lva_ranks.royal_rank
                    - lva_rank(move.moved_piece(info.get_state())));
            if(!solid_ordering) capture_score += mvvlva_capture_base;
            if(score_exposure)
            {
                // The cached result contains the arriving (possibly promoted)
                // piece and the changed occupancy, including en passant.
                const auto &arrival = *boards->result;
                const float arriving_value = capture_piece_value(arrival.get_piece(move.to.xy()));
                const float penalty = solid_ordering
                    ? solid_material_scale * arriving_value
                    : std::clamp((arriving_value - victim_value) * exposure_material_scale,
                        0.0f, maximum_exposure_penalty);
                if(penalty > 0.0f)
                {
                    const bool player = info.get_state().get_present().c();
                    const auto attackers = arrival.attacks_to(move.to.xy());
                    const auto friendly = player ? arrival.black() : arrival.white();
                    const auto hostile = player ? arrival.white() : arrival.black();
                    // A geometric defender suppresses this conservative penalty;
                    // it does not establish a legal or available recapture.
                    if((attackers & hostile) && !(attackers & friendly))
                        capture_score -= penalty;
                }
            }
            result += capture_score;
        }
    }
    else if(evaluate_captures)
    {
        switch(to_white(piece_name(move.captured_piece(info.get_state()))))
        {
            case QUEEN_W:
                result += w[QUEEN_CAPTURE];
                break;
            case PRINCESS_W:
                result += w[PRINCESS_CAPTURE];
                break;
            case DRAGON_W:
            case UNICORN_W:
                result += w[DRAGON_UNICORN_CAPTURE];
                break;
            case BISHOP_W:
            case KNIGHT_W:
                result += w[BISHOP_KNIGHT_CAPTURE];
                break;
            case ROOK_W:
            case COMMON_KING_W:
                result += w[ROOK_COMMON_KING_CAPTURE];
                break;
            case BRAWN_W:
                result += w[BRAWN_CAPTURE];
                break;
            case PAWN_W:
                result += w[PAWN_CAPTURE];
                break;
            default:
                break;
        }
    }

    if(score_exposure)
    {
        const piece_t moved = to_white(piece_name(move.moved_piece(info.get_state())));
        const piece_t arriving = to_white(piece_name(
            boards->result->get_piece(move.to.xy())));
        if((moved == PAWN_W || moved == BRAWN_W) && arriving != moved)
        {
            const float gain = std::max(0.0f,
                capture_piece_value(arriving) - capture_piece_value(moved));
            result += std::min(maximum_promotion_bonus, gain * exposure_material_scale);
        }
    }

    if(w[CHECK] != 0.0f && features.is_check(axis, coordinate))
        result += w[CHECK];

    if(w[SUPERPHYSICAL] != 0.0f
       && static_cast<bool>(move_type & special_move_t::SUPERPHYSICAL))
        result += w[SUPERPHYSICAL];

    if(solid_ordering)
    {
        if(axis >= info.first_branch_axis())
            result -= branch_arrival_penalty;
        else if(optional(move.from.l()) && optional(move.to.l()))
            result -= optional_move_penalty;
    }
    return result;
}

std::optional<move_score_table> move_evaluator::build_score_table(
    std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    move_score_table result(info.get_universe().dimension());
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
    {
        auto &scores = result[axis];
        index_t largest_coordinate = 0;
        bool has_coordinate = false;
        for(index_t coordinate : info.get_universe()[axis])
        {
            largest_coordinate = std::max(largest_coordinate, coordinate);
            has_coordinate = true;
        }
        if(has_coordinate)
            scores.resize(static_cast<std::size_t>(largest_coordinate) + 1);
        for(index_t coordinate : info.get_universe()[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            // Pruning can leave gaps in the coordinate IDs.
            scores[coordinate] = score(axis, coordinate);
        }
    }
    if(stop.stop_requested()) return std::nullopt;
    return result;
}
