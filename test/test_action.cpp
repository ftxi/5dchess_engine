#undef NDEBUG
#include <cassert>
#include "core/action.h"
#include "core/pgnparser.h"
#include "core/state.h"
#include "core/check_position.h"
#include <stdexcept>
#include <string>
#include "core/semimove.h"
#include "misc/utils.h"
#include <type_traits>
#include "state.h"
#include "client/game.h"
#include "core/hypercuboid.h"
#include "core/variants.h"
#include <iostream>

namespace action_tests
{
int run()
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
    const state s = state::from_ast<true>(*game);
    assert(full_move("(0T1)a2a4").lan(s, KNIGHT_W) == "(0T1)a2a4N");
    assert(full_move("(0T1)b1b2").lan(s, ROOK_W) == "(0T1)b1b2");

    const auto standard_game = pgnparser("[Board \"Standard\"]").parse_game();
    const state standard = state::from_ast<true>(*standard_game);
    const action e4 = action::from_vector({ext_move("(0T1)e2e4")}, standard);
    const action e4_with_irrelevant_promotion = action::from_vector(
        {ext_move(full_move("(0T1)e2e4"), QUEEN_W)}, standard);
    assert(e4 == e4_with_irrelevant_promotion);
    assert(!(e4 < e4_with_irrelevant_promotion) && !(e4_with_irrelevant_promotion < e4));
    const auto d4 = action::from_vector({ext_move("(0T1)d2d4")}, standard);
    assert((d4 < e4) != (e4 < d4));
    assert(!(e4 < e4));
    assert(action{} < e4 && !(e4 < action{}));
    const auto prefix = action::from_vector(
        {ext_move("(0T1)e2e4"), ext_move("(0T1)f2f4")}, standard);
    assert(e4 < prefix && !(prefix < e4));
    assert(e4.get_moves()[0].promote_to == NO_PIECE);
    const auto after_e4 = standard.can_apply(e4);
    assert(after_e4.has_value());
    const action e5 = action::from_vector({ext_move("(0T1)e7e5")}, *after_e4);

    const full_move quiet("(0T1)e2e4");
    assert(quiet.moved_piece(standard) == PAWN_W);
    assert(quiet.captured_piece(standard) == NO_PIECE);
    assert(quiet.move_type(standard) == special_move_t::NONE);
    state after_quiet = standard;
    assert(after_quiet.apply_move(quiet));
    const auto quiet_check_view = check_position::for_check_symbols(after_quiet);
    assert(!quiet_check_view.first_check(standard.get_present().c()));

    state capture_position = standard;
    assert(capture_position.apply_move(full_move("(0T1)e2e4")));
    assert(capture_position.submit());
    assert(capture_position.apply_move(full_move("(0T1)d7d5")));
    assert(capture_position.submit());
    const full_move capture("(0T2)e4d5");
    assert(capture.moved_piece(capture_position) == PAWN_W);
    assert(capture.captured_piece(capture_position) == PAWN_B);
    assert(static_cast<bool>(capture.move_type(capture_position)
                             & special_move_t::CAPTURE));

    state en_passant_position = standard;
    for (const char *notation : {"(0T1)e2e4", "(0T1)a7a6",
                                 "(0T2)e4e5", "(0T2)d7d5"}) {
        assert(en_passant_position.apply_move(full_move(notation)));
        assert(en_passant_position.submit());
    }
    const full_move en_passant("(0T3)e5d6");
    assert(en_passant.captured_piece(en_passant_position) == PAWN_B);
    assert(static_cast<bool>(en_passant.move_type(en_passant_position)
                             & special_move_t::EN_PASSANT));
    assert(static_cast<bool>(en_passant.move_type(en_passant_position)
                             & special_move_t::CAPTURE));
    assert(en_passant.pgn(en_passant_position, QUEEN_W,
                          pgn_options::SHOW_CAPTURE).find('x') != std::string::npos);

    const state small_castling = state::from_pgn<true>(
        "[Size \"4x4\"]\n[Board \"custom\"]\n[3k/4/4/1K1R:0:1:w]\n");
    const full_move castle("(0T1)b1d1");
    assert(castle.captured_piece(small_castling) == NO_PIECE);
    assert(static_cast<bool>(castle.move_type(small_castling)
                             & special_move_t::CASTLE_KINGSIDE));

    assert(static_cast<bool>(full_move("(0T1)a2a4").move_type(s)
                             & special_move_t::PROMOTION));

    const auto checking_game = pgnparser(R"(
[Size "4x4"]
[Board "custom"]
[3k/4/4/R2K:0:1:w]
)").parse_game();
    const state checking_position = state::from_ast<true>(*checking_game);
    const full_move checking_move("(0T1)a1a4");
    state after_check = checking_position;
    assert(after_check.apply_move(checking_move));
    const auto checking_view = check_position::for_check_symbols(after_check);
    assert(checking_view.first_check(checking_position.get_present().c()));
    assert(checking_move.pgn(checking_position, QUEEN_W, pgn_options::SHOW_MATE)
               .find('+') != std::string::npos);
    const action checking_action = action::from_vector(
        {ext_move(checking_move, checking_position)}, checking_position);
    assert(checking_action.pgn(checking_position, pgn_options::SHOW_MATE)
               .find('+') != std::string::npos);

    // Attacks created on inactive or future boards are not checks at the
    // opponent's present, even while a multi-move action is unfinished.
    for (bool mover : {false,true}) {
        const std::string fen = mover ? "r2k/4/4/3K" : "3k/4/4/R2K";
        const state multi(multiverse(timeline_layout::odd, {
            {0,1,mover,fen}, {1,3,mover,fen}, {2,1,mover,fen}
        },4,4));
        for (int l : {1,2}) {
            const int t = multi.get_timeline_end(l).t();
            const int from_y = mover ? 3 : 0, to_y = mover ? 0 : 3;
            const full_move optional_check(vec4(0,from_y,t,l),vec4(0,to_y,t,l));
            assert(optional_check.pgn(multi, QUEEN_W, pgn_options::SHOW_MATE)
                       .find('+') == std::string::npos);
            const full_move mandatory_quiet(vec4(0,from_y,1,0),
                vec4(0,mover ? 2 : 1,1,0));
            const action turn = action::from_vector({
                ext_move(optional_check,multi), ext_move(mandatory_quiet,multi)
            },multi);
            const auto notation = turn.pgn_advanced(multi,pgn_options::SHOW_MATE);
            assert(notation.first != "---INVALID ACTION---");
            assert(notation.first.find('+') == std::string::npos);
        }
        // A move on the active present board still receives a check symbol.
        const full_move present_check(vec4(0,mover ? 3 : 0,1,0),
            vec4(0,mover ? 0 : 3,1,0));
        assert(present_check.pgn(multi,QUEEN_W,pgn_options::SHOW_MATE)
                   .find('+') != std::string::npos);
    }

    assert(e4.pgn(standard, pgn_options::SHOW_OUTCOME)
        == e4.pgn(standard, pgn_options::SHOW_NOTHING));
    const auto basic = e4.pgn_advanced(standard, pgn_options::SHOW_NOTHING, e5);
    assert(!basic.second.has_value());
    const auto advanced = e4.pgn_advanced(standard, pgn_options::SHOW_MATE, e5);
    assert(advanced.second == mate_type::NONE);
    assert(e4.pgn(standard, pgn_options::SHOW_MATE)
        == e4.pgn_advanced(standard, pgn_options::SHOW_MATE).first);
    return 0;
}
} /* namespace action_tests */

namespace semimove_tests
{
int run()
{
    const state s = state::from_pgn<true>(R"(
[Board "Standard"]
)");

    const semimove physical = physical_move{full_move("(0T1)g1f3")};
    assert(physical.lan(s) == "(0T1)g1@Nf3");
    assert(physical.is<physical_move>());
    assert(physical.get_if<physical_move>() != nullptr);
    assert(physical.get<physical_move>().m == full_move("(0T1)g1f3"));
    assert(physical.hotspot() == vec4(6, 0, 1, 0));

    const semimove pawn = physical_move{full_move("(0T1)e2e4")};
    assert(pawn.lan(s) == "(0T1)e2@Pe4");

    const semimove arriving = arriving_move{full_move("(0T1)e1(-1T1)e3")};
    assert(arriving.lan(s) == "(0T1)e1@K>(-1T1)e3");
    assert(arriving.hotspot() == vec4(4, 2, 1, -1));

    const semimove departing = departing_move{vec4(4, 0, 1, 0)};
    assert(departing.lan(s) == "(0T1)e1>@K");
    assert(departing.hotspot() == vec4(4, 0, 1, 0));

    const semimove null = null_move{};
    assert(null.lan(s) == "@");
    bool threw = false;
    try
    {
        (void)null.hotspot();
    }
    catch(const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);

    const int kind = arriving.visit(overloads{
        [](const physical_move &) { return 0; },
        [](const arriving_move &) { return 1; },
        [](const departing_move &) { return 2; },
        [](const null_move &) { return 3; }
    });
    assert(kind == 1);
    return 0;
}
} /* namespace semimove_tests */

namespace apply_move_tests
{
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
    assert(before.normalize_promotion(move) == prepared);
    assert(before.normalize_promotion<false>(move, prepared.promote_to)
           == before.normalize_promotion<true>(move, prepared.promote_to));
    assert(checked.apply_move<false>(move));
    assert(unchecked.apply_move<true>(move));
    assert(checked_extended.apply_move<false>(prepared));
    assert(unchecked_extended.apply_move<true>(prepared));
    const auto full_result = before.can_apply(move);
    const auto ext_result = before.can_apply(prepared);
    assert(full_result && ext_result);
    assert_same(*full_result, checked);
    assert_same(*ext_result, checked_extended);
    assert(full_result->get_present() == before.get_present());
    if(prepared.promote_to == NO_PIECE)
        assert(!before.can_apply(ext_move(move, QUEEN_W)));
    else
        assert(!before.can_apply(ext_move(move, NO_PIECE)));
    assert_same(checked, unchecked);
    assert_same(checked, checked_extended);
    assert_same(checked, unchecked_extended);
}

int run()
{
    const state small(multiverse(timeline_layout::odd,
        {{0, 1, false, "3k/P3/4/K3"}}, 4, 4));
    for(const full_move move : {
        full_move("(999T1)a3a4"), full_move("(999T1)a3a2"),
        full_move("(0T0)a3a4"), full_move("(0T-1)a3a2"),
        full_move("(0T1)e3a4"), full_move("(0T1)a3e4"),
        full_move(vec4(-1, 2, 1, 0), vec4(0, 3, 1, 0)),
        full_move(vec4(0, 2, 1, 0), vec4(0, -1, 1, 0))})
    {
        assert(!small.normalize_promotion(move));
        assert(!small.can_apply(move));
        assert(!small.can_apply(ext_move(move, NO_PIECE)));
        state checked = small;
        assert(!checked.apply_move<false>(move));
        assert(!checked.apply_move<false>(ext_move(move, NO_PIECE)));
        assert_same(checked, small);
    }
    for(const full_move move : {full_move("(0T1)b2b3"), full_move("(0T1)d4c4")})
    {
        state checked = small;
        assert(!checked.apply_move<false>(move));
        assert_same(checked, small);
    }
    state historical = small;
    assert(historical.apply_move(full_move("(0T1)a3a4")));
    const state after_move = historical;
    assert(!historical.apply_move<false>(full_move("(0T1)a1b1")));
    assert(!historical.apply_move<false>(ext_move(full_move("(0T1)a1b1"), NO_PIECE)));
    assert_same(historical, after_move);

    for(bool black : {false, true})
    for(bool brawn : {false, true})
    for(auto rules : {promotion_options::QUEEN, promotion_options::KNIGHT,
                      promotion_options::NONE,
                      promotion_options::QUEEN | promotion_options::KNIGHT})
    {
        const std::string fen = black
            ? (brawn ? "k7/8/8/8/8/8/w7/7K" : "k7/8/8/8/8/8/p7/7K")
            : (brawn ? "7k/W7/8/8/8/8/8/K7" : "7k/P7/8/8/8/8/8/K7");
        multiverse boards(timeline_layout::odd, {{0, 1, black, fen}});
        const state before(boards, rules);
        const full_move promotion(black ? "(0T1)a2a1" : "(0T1)a7a8");
        compare_overloads(before, promotion);
        compare_overloads(before, full_move(black ? "(0T1)a8b8" : "(0T1)a1b1"));

        const ext_move prepared(promotion, before);
        for(piece_t choice : {NO_PIECE, QUEEN_W, KNIGHT_W, KING_W, KNIGHT_B})
        {
            assert(before.normalize_promotion<false>(promotion, choice)
                   == before.normalize_promotion<true>(promotion, choice));
        }
        assert(prepared.promote_to == default_promote_to(rules));
        state result = before;
        assert(result.apply_move<true>(promotion));
        const auto [t, player] = result.get_timeline_end(0).get();
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
            const auto underpromotion = before.can_apply(knight);
            assert(underpromotion);
            assert_same(*underpromotion, checked);
        }
    }
    return 0;
}
} /* namespace apply_move_tests */

namespace promotions_tests
{
namespace
{
constexpr const char *position = R"(
[Timeline "odd"]
[Size "8x8"]
[5bk1/5N2/7P/6K1/8/8/8/8:0:1:w]

1. h7+ / Kxf7
2. h8={}
)";

std::string promotion_game(const std::string &header, char piece)
{
    std::string result = position;
    result.replace(result.find("{}"), 2, std::string(1, piece));
    if(!header.empty())
    {
        result.insert(1, "[Promotions \"" + header + "\"]\n");
    }
    return result;
}

bool rejects(const std::string &pgn)
{
    try
    {
        (void)game::from_pgn(pgn);
        return false;
    }
    catch(const std::exception &)
    {
        return true;
    }
}
}

int run()
{
    assert(parse_promotions("") == promotion_options::NONE);
    assert(parse_promotions(" , \t") == promotion_options::NONE);
    assert(format_promotions(promotion_options::NONE).empty());
    assert(parse_promotions("Q") == promotion_options::QUEEN);
    assert(parse_promotions("R B N,  Q")
        == (promotion_options::QUEEN | promotion_options::ROOK
            | promotion_options::BISHOP | promotion_options::KNIGHT));
    assert(format_promotions(parse_promotions("D U C N B R S Q")) == "*");
    assert(format_promotions(parse_promotions("R B N,  Q")) == "QRBN");
    assert(default_promote_to(parse_promotions("D U C N B R S")) == PRINCESS_W);

    for(const std::string invalid : {"q", "P", "W", "K", "Y", "*Q"})
    {
        bool threw = false;
        try
        {
            (void)parse_promotions(invalid);
        }
        catch(const invalid_promotions &)
        {
            threw = true;
        }
        assert(threw);
    }

    game knight = game::from_pgn(promotion_game("N", 'N'));
    assert(knight.get_promotion_options() == promotion_options::KNIGHT);
    const std::string knight_output = knight.show_pgn();
    assert(knight_output.contains("[Promotions \"N\"]"));
    assert(knight_output.contains("h8=N"));
    knight.set_header("promotions", "Q");
    assert(knight.show_pgn().contains("[Promotions \"N\"]"));

    assert(rejects(promotion_game("", 'N')));
    assert(rejects(promotion_game("S", 'Q')));
    assert(!rejects(promotion_game("S", 'S')));
    assert(!rejects(promotion_game("R B N,  Q", 'N')));
    assert(!rejects(promotion_game("*", 'D')));

    std::string bare_knight = promotion_game("QN", 'N');
    bare_knight.erase(bare_knight.find("=N"), 1);
    game bare_knight_game = game::from_pgn(bare_knight);
    assert(bare_knight_game.show_pgn().contains("h8=N"));

    std::string long_bishop = promotion_game("QB", 'B');
    long_bishop.replace(long_bishop.find("h8=B"), 4, "h7h8B");
    game long_bishop_game = game::from_pgn(long_bishop);
    assert(long_bishop_game.show_pgn().contains("h8=B"));

    std::string trailing_piece = bare_knight;
    trailing_piece.insert(trailing_piece.find("h8N") + 3, "N");
    assert(rejects(trailing_piece));

    std::string implicit_princess = promotion_game("S", 'S');
    implicit_princess.erase(implicit_princess.find("=S"), 2);
    game princess = game::from_pgn(implicit_princess);
    assert(princess.show_pgn().contains("h8=S"));

    game default_game = game::from_pgn(promotion_game("", 'Q'));
    assert(default_game.get_promotion_options() == promotion_options::QUEEN);
    assert(default_game.get_header("promotions") == "Q");
    assert(!default_game.show_pgn().contains("[Promotions"));

    game explicit_default_game = game::from_pgn(promotion_game("Q", 'Q'));
    assert(explicit_default_game.get_header("Promotions") == "Q");
    assert(explicit_default_game.show_pgn().contains("[Promotions \"Q\"]"));

    const std::string before_promotion = R"(
[Promotions "Q"]
[Timeline "odd"]
[Size "8x8"]
[5bk1/5N2/7P/6K1/8/8/8/8:0:1:w]

1. h7+ / Kxf7
)";
    const auto parsed = pgnparser(before_promotion).parse_game();
    assert(parsed.has_value());
    const state promotion_state = state::from_ast<true>(*parsed);
    const auto [fm, pt, candidates] = promotion_state.parse_move("h8=Q");
    assert(fm.has_value());
    const action implicit_queen = action::from_vector({ext_move(*fm, promotion_state)}, promotion_state);
    const action explicit_queen = action::from_vector(
        {ext_move(*fm, QUEEN_W)}, promotion_state);
    assert(implicit_queen == explicit_queen);
    assert(implicit_queen.get_moves()[0].promote_to == QUEEN_W);
    std::string multiple_promotions = before_promotion;
    multiple_promotions.replace(multiple_promotions.find("\"Q\""), 3, "\"QR\"");
    const state promotion_order_state = state::from_pgn<true>(multiple_promotions);
    const auto rook = action::from_vector({ext_move(*fm, ROOK_W)}, promotion_order_state);
    const auto queen = action::from_vector({ext_move(*fm, QUEEN_W)}, promotion_order_state);
    assert((rook < queen) != (queen < rook));
    assert(!promotion_state.can_apply(ext_move(*fm, KNIGHT_W)));
    assert(!promotion_state.can_apply(ext_move(*fm, KNIGHT_B)));

    const std::string no_promotion_before = R"(
[Promotions ""]
[Timeline "odd"]
[Size "8x8"]
[5bk1/5N2/7P/6K1/8/8/8/8:0:1:w]

1. h7+ / Kxf7
)";
    const auto no_promotion_parsed = pgnparser(no_promotion_before).parse_game();
    assert(no_promotion_parsed.has_value());
    const state no_promotion_state = state::from_ast<true>(*no_promotion_parsed);
    assert(no_promotion_state.get_promotion_options() == promotion_options::NONE);
    const auto [no_fm, no_pt, no_candidates] = no_promotion_state.parse_move("h8");
    assert(no_fm.has_value());
    assert(!no_pt.has_value());
    const auto unchanged_pawn = no_promotion_state.can_apply(*no_fm);
    assert(unchanged_pawn.has_value());
    const auto [pawn_t, pawn_c] = unchanged_pawn->get_timeline_end(no_fm->to.l()).get();
    assert(unchanged_pawn->get_piece(
        vec4(no_fm->to.x(), no_fm->to.y(), pawn_t, no_fm->to.l()), pawn_c)
        == PAWN_W);
    assert(!no_promotion_state.can_apply(ext_move(*no_fm, QUEEN_W)));

    const action no_promotion_action = action::from_vector(
        {ext_move(*no_fm, no_promotion_state)}, no_promotion_state);
    assert(no_promotion_action.get_moves()[0].promote_to == NO_PIECE);
    const std::string no_promotion_pgn = no_promotion_action.pgn(no_promotion_state);
    assert(no_promotion_pgn.contains("h7h8"));
    assert(!no_promotion_pgn.contains('='));
    assert(no_promotion_action.lan(no_promotion_state).find('\0') == std::string::npos);

    auto [hc_info, search_space] = HC_info::build_HC(no_promotion_state);
    bool generated_no_promotion = false;
    for(const moveseq &moves : hc_info.search(std::move(search_space)))
    {
        if(std::find(moves.begin(), moves.end(), *no_fm) != moves.end())
        {
            generated_no_promotion = true;
            break;
        }
    }
    assert(generated_no_promotion);

    std::string no_promotion_game = no_promotion_before + "2. h8\n";
    game no_promotion = game::from_pgn(no_promotion_game);
    const std::string no_promotion_output = no_promotion.show_pgn();
    assert(no_promotion_output.contains("[Promotions \"\"]"));
    assert(no_promotion_output.contains("h7h8"));
    assert(!no_promotion_output.contains("h8="));

    assert(rejects(no_promotion_before + "2. h8=Q\n"));
    assert(rejects(no_promotion_before + "2. h8Q\n"));

    state unsafe_state = promotion_state;
    assert(unsafe_state.apply_move<true>(ext_move(*fm, KNIGHT_W)));
    const auto [end_t, end_c] = unsafe_state.get_timeline_end(fm->to.l()).get();
    assert(unsafe_state.get_piece(
        vec4(fm->to.x(), fm->to.y(), end_t, fm->to.l()), end_c) == KNIGHT_W);

    assert(sizeof(promotion_options) == 1);
    return 0;
}
} /* namespace promotions_tests */

int main()
{
    std::cerr << "Running action tests\n";
    if(const int result = action_tests::run(); result != 0)
        return result;
    std::cerr << "Running semimove tests\n";
    if(const int result = semimove_tests::run(); result != 0)
        return result;
    std::cerr << "Running apply_move tests\n";
    if(const int result = apply_move_tests::run(); result != 0)
        return result;
    std::cerr << "Running promotions tests\n";
    if(const int result = promotions_tests::run(); result != 0)
        return result;
    return 0;
}
