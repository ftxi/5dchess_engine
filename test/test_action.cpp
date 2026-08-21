#include <cassert>
#include "core/action.h"
#include "core/pgnparser.h"
#include "core/state.h"

int main()
{
    const ext_move default_promotion("(0T0)e7e8");
    assert(default_promotion.fm == full_move("(0T0)e7e8"));
    assert(default_promotion.promote_to == QUEEN_W);

    const ext_move knight_promotion("(0T0)e7e8N");
    assert(knight_promotion.fm == full_move("(0T0)e7e8"));
    assert(knight_promotion.promote_to == KNIGHT_W);

    const ext_move timeline_promotion("(0T0)e7>>(1T1)e8R");
    assert(timeline_promotion.fm == full_move("(0T0)e7>>(1T1)e8"));
    assert(timeline_promotion.promote_to == ROOK_W);

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
    const auto after_e4 = standard.can_apply(e4);
    assert(after_e4.has_value());
    const action e5 = action::from_vector({ext_move("(0T1)e7e5")}, *after_e4);

    assert(e4.pgn(standard, pgn_options::SHOW_OUTCOME)
        == e4.pgn(standard, pgn_options::SHOW_NOTHING));
    const auto basic = e4.pgn_advanced(standard, pgn_options::SHOW_NOTHING, e5);
    assert(!basic.second.has_value());
    const auto advanced = e4.pgn_advanced(standard, pgn_options::SHOW_MATE, e5);
    assert(advanced.second == mate_type::NONE);
    assert(e4.pgn(standard, pgn_options::SHOW_MATE)
        == e4.pgn_advanced(standard, pgn_options::SHOW_MATE).first);
}
