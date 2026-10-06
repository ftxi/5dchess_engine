#include "move_feature.h"

#include <cassert>

#include "hypercuboid.h"

namespace
{
template<check_position::check_kind Kind>
bool check_move_boards(
    check_position& checks,
    const state& before,
    full_move move,
    const board* result,
    const board* departure
)
{
    using board_overlay = check_position::board_overlay;

    const bool move_player = before.get_present().second;
    const bool is_physical_move = move.from.tl() == move.to.tl();
    const int arrival_line = move.new_position(before).l();

    // The result board first appears on the next turn. The candidate view also
    // presents that same board at the moving player's following turn.
    const turn_t arrival_start = next_turn({move.to.t(), move_player});
    const board_overlay arrival{
        arrival_line, arrival_start.first, arrival_start.second, result
    };

    if (is_physical_move) 
    {
        return checks.gives_check<Kind>(arrival, move_player);
    }

    assert(departure != nullptr);
    const turn_t departure_start = next_turn({move.from.t(), move_player});
    const board_overlay departure_view{
        move.from.l(), departure_start.first, departure_start.second, departure
    };
    return checks.gives_check<Kind>(arrival, move_player, departure_view);
}
} /* anonymous namespace */

bool has_physical_check(const board& position, bool defender)
{
    const bitboard_t defending = defender
        ? position.black() : position.white();
    for (int square : marked_pos(position.royal() & defending))
        if (position.is_under_attack(square, defender)) return true;
    return false;
}

bool has_latent_king_threat(const board& result, int king_xy, bool player)
{
    const bitboard_t enemy = player ? result.white() : result.black();
    return static_cast<bool>(
        (knight_jump1_attack(king_xy) & enemy & result.knight())
        | (rook_copy_mask(king_xy,1) & enemy & result.lbishop())
        | (bishop_copy_mask(king_xy,1) & enemy & result.lunicorn()));
}

semimove_feature::semimove_feature(const HC_info& value) : info(value) {}

template<check_position::check_kind Kind>
bool semimove_feature::check(index_t axis, index_t coordinate)
{
    const auto boards = info.get_move_boards(axis, coordinate);
    if (!boards) return false;
    if (!checks) checks.emplace(check_position::for_move_candidates(info.s));
    return check_move_boards<Kind>(*checks, info.s, boards->move,
                                   boards->result, boards->departure);
}

bool semimove_feature::is_physical_check(index_t axis, index_t coordinate)
{
    const auto boards = info.get_move_boards(axis, coordinate);
    if (!boards) return false;
    const bool defender = !info.s.get_present().second;
    return has_physical_check(*boards->result, defender)
        || (boards->departure
            && has_physical_check(*boards->departure, defender));
}

bool semimove_feature::is_sp_check(index_t axis, index_t coordinate)
{
    return check<check_position::check_kind::superphysical>(axis, coordinate);
}

bool semimove_feature::is_historical_check(index_t axis, index_t coordinate)
{
    return check<check_position::check_kind::historical>(axis, coordinate);
}

bool semimove_feature::is_check(index_t axis, index_t coordinate)
{
    return is_physical_check(axis, coordinate)
        || is_sp_check(axis, coordinate);
}

bool semimove_feature::has_latent_king_threat(index_t axis,
                                               index_t coordinate) const
{
    const auto boards = info.get_move_boards(axis, coordinate);
    if (!boards || to_white(boards->move.moved_piece(info.s)) != KING_W)
        return false;
    return ::has_latent_king_threat(*boards->result, boards->move.to.xy(),
                                     info.s.get_present().second);
}
