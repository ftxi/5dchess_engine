#undef NDEBUG
#include <cassert>
#include "core/action.h"
#include "core/pgnparser.h"
#include "core/state.h"

int main()
{
    const ext_move default_promotion("(0T0)e7e8");
    assert(default_promotion.fm == full_move("(0T0)e7e8"));
    assert(default_promotion.promote_to == NO_PIECE);

    const ext_move knight_promotion("(0T0)e7e8N");
    assert(knight_promotion.fm == full_move("(0T0)e7e8"));
    assert(knight_promotion.promote_to == KNIGHT_W);

    const ext_move timeline_promotion("(0T0)e7>>(1T1)e8R");
    assert(timeline_promotion.fm == full_move("(0T0)e7>>(1T1)e8"));
    assert(timeline_promotion.promote_to == ROOK_W);

    const ext_move ordinary_move("(0T0)e2e4");
    assert(ordinary_move.to_string() == "(0T0)e2e4");

    const auto game = pgnparser(R"(
[Size "4x4"]
[Board "custom"]
[nbrk/3p*/P*3/KRBN:0:1:w]
)").parse_game();
    const state s(*game);
    assert(full_move("(0T1)a2a4").lan(s, KNIGHT_W) == "(0T1)a2a4N");
    assert(full_move("(0T1)b1b2").lan(s, ROOK_W) == "(0T1)b1b2");

    const auto standard_game = pgnparser("[Board \"Standard\"]").parse_game();
    const state standard(*standard_game);
    const action e4 = action::from_vector({ext_move("(0T1)e2e4")}, standard);
    const action e4_with_irrelevant_promotion = action::from_vector(
        {ext_move(full_move("(0T1)e2e4"), QUEEN_W)}, standard);
    assert(e4 == e4_with_irrelevant_promotion);
    assert(e4.get_moves()[0].promote_to == NO_PIECE);
    const auto after_e4 = standard.can_apply(e4);
    assert(after_e4.has_value());
    const action e5 = action::from_vector({ext_move("(0T1)e7e5")}, *after_e4);

    const state::move_info quiet_info = standard.get_move_info(
        full_move("(0T1)e2e4"));
    assert(quiet_info.new_state);
    assert(quiet_info.moved_piece == PAWN_W);
    assert(quiet_info.captured_piece == NO_PIECE);
    assert(quiet_info.special_move == special_move_t::NONE);
    assert(quiet_info.check_type == check_type_t::NONE);

    state capture_position = standard;
    assert(capture_position.apply_move(full_move("(0T1)e2e4")));
    assert(capture_position.submit());
    assert(capture_position.apply_move(full_move("(0T1)d7d5")));
    assert(capture_position.submit());
    const state::move_info capture_info = capture_position.get_move_info(
        full_move("(0T2)e4d5"));
    assert(capture_info.moved_piece == PAWN_W);
    assert(capture_info.captured_piece == PAWN_B);
    assert(static_cast<bool>(capture_info.special_move & special_move_t::CAPTURE));

    const state::move_info promotion_info = s.get_move_info(
        full_move("(0T1)a2a4"), KNIGHT_W);
    assert(static_cast<bool>(promotion_info.special_move & special_move_t::PROMOTION));

    const auto checking_game = pgnparser(R"(
[Size "4x4"]
[Board "custom"]
[3k/4/4/R2K:0:1:w]
)").parse_game();
    const state checking_position(*checking_game);
    const state::move_info check_info = checking_position.get_move_info(
        full_move("(0T1)a1a4"));
    assert(static_cast<bool>(check_info.check_type & check_type_t::PHYSICAL_CHECK));
    assert(static_cast<bool>(check_info.check_type));

    assert(e4.pgn(standard, pgn_options::SHOW_OUTCOME)
        == e4.pgn(standard, pgn_options::SHOW_NOTHING));
    const auto basic = e4.pgn_advanced(standard, pgn_options::SHOW_NOTHING, e5);
    assert(!basic.second.has_value());
    const auto advanced = e4.pgn_advanced(standard, pgn_options::SHOW_MATE, e5);
    assert(advanced.second == mate_type::NONE);
    assert(e4.pgn(standard, pgn_options::SHOW_MATE)
        == e4.pgn_advanced(standard, pgn_options::SHOW_MATE).first);
}
