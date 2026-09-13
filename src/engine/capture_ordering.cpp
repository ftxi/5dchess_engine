#include "capture_ordering.h"

#include "move_info_evaluation.h"
#include "utils.h"

namespace
{
bool cheap_capture(const state &s, full_move move)
{
    const vec4 from = move.from;
    const vec4 to = move.to;
    const bool player = s.get_present().second;
    const auto board = s.get_board(from.l(), from.t(), player);

    // Physical captures include en passant, whose destination square is empty.
    if(from.l() == to.l() && from.t() == to.t())
    {
        if(board->get_piece(to.xy()) != NO_PIECE) return true;
        return static_cast<bool>(board->lpawn() & pmask(from.xy()))
            && from.x() != to.x();
    }

    // A superphysical capture is represented by its arrival. The paired
    // departure is deliberately neutral, so a move is weighted only once.
    return s.get_board(to.l(), to.t(), player)->get_piece(to.xy())
        != NO_PIECE;
}
}

bool is_capture_for_weighting(const state &s, full_move move)
{
    return cheap_capture(s, move);
}

float capture_semimove_score(
    const state &s, const semimove &move, float capture_score)
{
    return move.visit(overloads{
        [&](const physical_move &physical) {
            return is_capture_for_weighting(s, physical.m)
                ? capture_score : 0.0f;
        },
        [&](const arriving_move &arriving) {
            return is_capture_for_weighting(s, arriving.m)
                ? capture_score : 0.0f;
        },
        [](const departing_move &) { return 0.0f; },
        [](const null_move &) { return 0.0f; },
    });
}

std::optional<std::vector<std::vector<float>>> capture_coordinate_scores(
    const HC_info &info, std::stop_token stop, float capture_score)
{
    std::vector<std::vector<float>> result(info.universe.dimension());
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
    {
        auto &axis_scores = result[axis];
        // Scores are indexed by the coordinate used by HC_info::get_semimove,
        // rather than by the compact position within the universe set.  The
        // latter can contain holes after illegal arriving entries are pruned.
        // Keeping those coordinates aligned avoids reading a score for a
        // different move (or indexing past the end) in scored_HC_ordering.
        index_t largest_coordinate = 0;
        bool has_coordinate = false;
        for(index_t coordinate : info.universe[axis])
        {
            largest_coordinate = std::max(largest_coordinate, coordinate);
            has_coordinate = true;
        }
        if(has_coordinate)
            axis_scores.resize(
                static_cast<std::size_t>(largest_coordinate) + 1, 0.0f);
        for(index_t coordinate : info.universe[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            axis_scores[coordinate] = capture_semimove_score(
                info.s, info.get_semimove(axis, coordinate), capture_score);
        }
    }
    return result;
}
