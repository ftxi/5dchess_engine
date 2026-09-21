#undef NDEBUG
#include <cassert>
#include <type_traits>
#include "state.h"

static_assert(!std::is_constructible_v<ext_move, full_move>);
static_assert(!std::is_constructible_v<ext_move, vec4, vec4>);
static_assert(std::is_constructible_v<ext_move, full_move, const state &>);
static_assert(std::is_constructible_v<ext_move, full_move, piece_t>);

template<class S>
concept old_apply_signature = requires(S &s, full_move mv) {
    s.template apply_move<true>(mv, QUEEN_W);
};
static_assert(!old_apply_signature<state>);

void assert_same(const state &a, const state &b)
{
    assert(a.get_boards() == b.get_boards());
    assert(a.get_present() == b.get_present());
}

void compare_overloads(const state &before, full_move move)
{
    state checked = before, unchecked = before;
    state checked_extended = before, unchecked_extended = before;
    const ext_move prepared(move, before);
    assert(checked.apply_move<false>(move));
    assert(unchecked.apply_move<true>(move));
    assert(checked_extended.apply_move<false>(prepared));
    assert(unchecked_extended.apply_move<true>(prepared));
    assert_same(checked, unchecked);
    assert_same(checked, checked_extended);
    assert_same(checked, unchecked_extended);
    const auto info = before.get_move_info(move);
    assert(info.new_state);
    assert_same(checked, *info.new_state);
}

int main()
{
    for(bool black : {false, true})
    for(bool brawn : {false, true})
    for(auto rules : {promotion_options::QUEEN, promotion_options::KNIGHT,
                      promotion_options::NONE,
                      promotion_options::QUEEN | promotion_options::KNIGHT})
    {
        const std::string fen = black
            ? (brawn ? "k7/8/8/8/8/8/w7/7K" : "k7/8/8/8/8/8/p7/7K")
            : (brawn ? "7k/W7/8/8/8/8/8/K7" : "7k/P7/8/8/8/8/8/K7");
        multiverse_odd boards({{0, 1, black, fen}});
        const state before(boards, rules);
        const full_move promotion(black ? "(0T1)a2a1" : "(0T1)a7a8");
        compare_overloads(before, promotion);
        compare_overloads(before, full_move(black ? "(0T1)a8b8" : "(0T1)a1b1"));

        const ext_move prepared(promotion, before);
        assert(prepared.promote_to == default_promote_to(rules));
        state result = before;
        assert(result.apply_move<true>(promotion));
        const auto [t, player] = result.get_timeline_end(0);
        const piece_t expected_white = rules == promotion_options::NONE
            ? (brawn ? BRAWN_W : PAWN_W) : default_promote_to(rules);
        assert(result.get_piece(vec4(0, black ? 0 : 7, t, 0), player)
               == (black ? to_black(expected_white) : expected_white));

        if(rules != promotion_options::NONE)
        {
            state missing = before;
            // Extended moves are prepared data, not a request for a default.
            assert(!missing.apply_move<false>(ext_move(promotion, NO_PIECE)));
            assert_same(missing, before);
        }
        state invalid = before;
        assert(!invalid.apply_move<false>(ext_move(promotion, KING_W)));
        assert_same(invalid, before);

        if(can_promote_to(rules, KNIGHT_W))
        {
            state checked = before, unchecked = before;
            const ext_move knight(promotion, KNIGHT_W);
            assert(checked.apply_move<false>(knight));
            assert(unchecked.apply_move<true>(knight));
            assert_same(checked, unchecked);
            const auto info = before.get_move_info(promotion, KNIGHT_W);
            assert(info.new_state);
            assert_same(checked, *info.new_state);
        }
    }
}
