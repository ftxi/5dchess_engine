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
    semimove_feature evaluator(info);
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
                const auto move = info.get_move_boards(axis, coordinate);
                scores[coordinate] = move
                    && move->move.captured_piece(info.s) != NO_PIECE
                    ? weights.values[move_info_weights::QUEEN_CAPTURE] : 0.0f;
            }
            else
            {
                scores[coordinate] = hc_move_info_score(
                    info, evaluator, axis, coordinate, weights);
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
