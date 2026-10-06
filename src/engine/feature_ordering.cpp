#include "feature_ordering.h"

#include <algorithm>
#include <numeric>
#include "utils.h"

namespace
{
bool uniform_capture_profile(const move_info_weights &weights)
{
    const float capture = weights.values[move_info_weights::QUEEN_CAPTURE];
    for(std::size_t i = 0; i < weights.values.size(); ++i)
    {
        if(i >= move_info_weights::QUEEN_CAPTURE
           && i <= move_info_weights::PAWN_CAPTURE)
        {
            if(weights.values[i] != capture) return false;
        }
        else if(weights.values[i] != 0.0f) return false;
    }
    return true;
}

bool is_capture(const state &s, full_move move)
{
    const vec4 from = move.from;
    const vec4 to = move.to;
    const bool player = s.get_present().second;
    if(from.tl() == to.tl())
    {
        const auto board = s.get_board(from.l(), from.t(), player);
        if(board->get_piece(to.xy()) != NO_PIECE) return true;
        // En passant captures a pawn beside the otherwise empty destination.
        return static_cast<bool>(board->lpawn() & pmask(from.xy()))
            && from.x() != to.x();
    }
    return s.get_board(to.l(), to.t(), player)->get_piece(to.xy()) != NO_PIECE;
}

bool is_capture(const state &s, const semimove &move)
{
    return move.visit(overloads{
        [&](const physical_move &physical) { return is_capture(s, physical.m); },
        [&](const arriving_move &arriving) { return is_capture(s, arriving.m); },
        [](const departing_move &) { return false; },
        [](const null_move &) { return false; },
    });
}
} // namespace

move_info_weights capture_ordering_weights(bool include_checks)
{
    move_info_weights weights{};
    for(std::size_t i = move_info_weights::QUEEN_CAPTURE;
        i <= move_info_weights::PAWN_CAPTURE; ++i)
        weights.values[i] = capture_feature_score;
    if(include_checks) weights.values[move_info_weights::CHECK] = check_feature_score;
    return weights;
}

std::optional<feature_score_table> feature_coordinate_scores(
    const HC_info &info, const move_info_weights &weights, std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    // Equal capture weights collapse to one capture indicator. Avoid building
    // the richer move metadata for the capture-only engine.
    const bool quick_capture = uniform_capture_profile(weights);
    std::optional<hc_move_evaluation> evaluator;
    if(!quick_capture)
        evaluator.emplace(info, weights.values[move_info_weights::CHECK] != 0.0f);
    feature_score_table result(info.universe.dimension());
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
    {
        auto &scores = result[axis];
        index_t largest_coordinate = 0;
        bool has_coordinate = false;
        for(index_t coordinate : info.universe[axis])
        {
            largest_coordinate = std::max(largest_coordinate, coordinate);
            has_coordinate = true;
        }
        if(has_coordinate)
            scores.resize(static_cast<std::size_t>(largest_coordinate) + 1);
        for(index_t coordinate : info.universe[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            // Pruning can leave gaps in the coordinate IDs.
            if(quick_capture)
            {
                scores[coordinate] = is_capture(
                    info.s, info.get_semimove(axis, coordinate))
                    ? weights.values[move_info_weights::QUEEN_CAPTURE] : 0.0f;
            }
            else
            {
                const auto features = evaluator->features(axis, coordinate);
                scores[coordinate] = std::inner_product(
                    features.begin(), features.end(), weights.values.begin(), 0.0f);
            }
        }
    }
    if(stop.stop_requested()) return std::nullopt;
    return result;
}

std::optional<feature_score_table> capture_feature_scores(
    const HC_info &info, std::stop_token stop)
{
    return feature_coordinate_scores(info, capture_ordering_weights(false), stop);
}

std::optional<feature_score_table> capture_check_feature_scores(
    const HC_info &info, std::stop_token stop)
{
    return feature_coordinate_scores(info, capture_ordering_weights(true), stop);
}
