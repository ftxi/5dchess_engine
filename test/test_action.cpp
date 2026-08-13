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
    assert(full_move("(0T1)a2a4").lan(s, KNIGHT_W) == "(0T1)Pa2(0T1)a4N");
    assert(full_move("(0T1)b1b2").lan(s, ROOK_W) == "(0T1)Rb1(0T1)b2");
}
