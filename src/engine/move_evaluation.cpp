#include "move_evaluation.h"

#include <algorithm>

#include "hypercuboid.h"

move_evaluator::move_evaluator(const HC_info& info,
                               move_evaluation_weights weights)
    : info(info), weights(weights), features(info),
      evaluate_captures(std::any_of(
          weights.values.begin() + move_evaluation_weights::QUEEN_CAPTURE,
          weights.values.begin() + move_evaluation_weights::PAWN_CAPTURE + 1,
          [](float weight) { return weight != 0.0f; }))
{}

float move_evaluator::score(index_t axis, index_t coordinate)
{
    const auto boards = info.get_move_boards(axis, coordinate);
    if(!boards) return 0.0f;

    using enum move_evaluation_weights::indices;
    const auto& w = weights.values;
    const full_move& move = boards->move;
    float result = 0.0f;

    bool king_move = false;
    if(w[LATENT_KING_THREAT] != 0.0f || w[NORMAL_KING_MOVE] != 0.0f
       || w[KING_BRANCH] != 0.0f)
    {
        const piece_t moved = to_white(piece_name(move.moved_piece(info.s)));
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
        move_type = move.move_type(info.s);

    if(king_move && w[KING_BRANCH] != 0.0f
       && static_cast<bool>(move_type & special_move_t::BRANCHING))
        result += w[KING_BRANCH];

    if(evaluate_captures)
    {
        switch(to_white(piece_name(move.captured_piece(info.s))))
        {
            case QUEEN_W:
            case ROYAL_QUEEN_W:
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

    if(w[CHECK] != 0.0f && features.is_check(axis, coordinate))
        result += w[CHECK];

    if(w[SUPERPHYSICAL] != 0.0f
       && static_cast<bool>(move_type & special_move_t::SUPERPHYSICAL))
        result += w[SUPERPHYSICAL];

    return result;
}

std::optional<move_score_table> move_evaluator::build_score_table(
    std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    move_score_table result(info.universe.dimension());
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
            scores[coordinate] = score(axis, coordinate);
        }
    }
    if(stop.stop_requested()) return std::nullopt;
    return result;
}
