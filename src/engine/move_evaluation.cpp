#include "move_evaluation.h"

#include <algorithm>

#include "hypercuboid.h"

namespace
{
// Ordinal ranks follow the existing 5D capture values. Royals used as
// attackers rank last; king victims remain check/legality concerns.
int capture_rank(piece_t piece)
{
    switch(to_white(piece_name(piece)))
    {
        case PAWN_W: return 1;
        case BRAWN_W: return 2;
        case ROOK_W:
        case COMMON_KING_W: return 3;
        case BISHOP_W:
        case KNIGHT_W: return 4;
        case UNICORN_W:
        case DRAGON_W: return 5;
        case PRINCESS_W: return 6;
        case QUEEN_W: return 7;
        case KING_W:
        case ROYAL_QUEEN_W: return 8;
        default: return 0;
    }
}
} /* anonymous namespace */

move_evaluator::move_evaluator(const HC_info &info,
                               move_evaluation_weights weights,
                               capture_ordering captures)
    : info(info), weights(weights), features(info),
      evaluate_captures(std::any_of(
          weights.values.begin() + move_evaluation_weights::QUEEN_CAPTURE,
          weights.values.begin() + move_evaluation_weights::PAWN_CAPTURE + 1,
          [](float weight) { return weight != 0.0f; })), captures(captures)
{}

float move_evaluator::score(index_t axis, index_t coordinate)
{
    const auto boards = info.get_move_boards(axis, coordinate);
    if(!boards) return 0.0f;

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
        // Keep zccpw's capture/check priority groups: captures score 480..583,
        // checks add 300. A victim rank always outweighs attacker preference.
        const int victim_rank = victim == ROYAL_QUEEN_W ? 7 : capture_rank(victim);
        if(victim_rank > 0 && victim != KING_W)
        {
            float capture_score = 480.0f + 16 * (victim_rank - 1)
                + (8 - capture_rank(move.moved_piece(info.get_state())));
            if(captures == capture_ordering::mvvlva_exposure)
            {
                // The cached result contains the arriving (possibly promoted)
                // piece and the changed occupancy, including en passant.
                const auto &arrival = *boards->result;
                const float loss = std::max(0.0f,
                    capture_piece_value(arrival.get_piece(move.to.xy()))
                    - capture_piece_value(victim));
                if(loss > 0.0f)
                {
                    const bool player = info.get_state().get_present().c();
                    const auto attackers = arrival.attacks_to(move.to.xy());
                    const auto friendly = player ? arrival.black() : arrival.white();
                    const auto hostile = player ? arrival.white() : arrival.black();
                    // A geometric defender suppresses this conservative penalty;
                    // it does not establish a legal or available recapture.
                    if((attackers & hostile) && !(attackers & friendly))
                        capture_score -= std::min(128.0f, loss / 10.0f);
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

    if(captures == capture_ordering::mvvlva_exposure)
    {
        const piece_t moved = to_white(piece_name(move.moved_piece(info.get_state())));
        const piece_t arriving = to_white(piece_name(
            boards->result->get_piece(move.to.xy())));
        if((moved == PAWN_W || moved == BRAWN_W) && arriving != moved)
        {
            const float gain = std::max(0.0f,
                capture_piece_value(arriving) - capture_piece_value(moved));
            // A separate bound keeps checking quiet promotions below captures
            // (<= 348 versus >= 352), and checking captures above all non-checks.
            result += std::min(48.0f, gain / 10.0f);
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
