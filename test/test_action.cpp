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

    const full_move quiet("(0T1)e2e4");
    assert(quiet.moved_piece(standard) == PAWN_W);
    assert(quiet.captured_piece(standard) == NO_PIECE);
    assert(quiet.move_type(standard) == special_move_t::NONE);
    state after_quiet = standard;
    assert(after_quiet.apply_move(quiet));
    const state quiet_check_view = after_quiet.phantom(!standard.get_present().second);
    assert(!quiet_check_view.find_checks(standard.get_present().second).first());

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
    for (const char* notation : {"(0T1)e2e4", "(0T1)a7a6",
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

    const state small_castling(*pgnparser(
        "[Size \"4x4\"]\n[Board \"custom\"]\n[3k/4/4/1K1R:0:1:w]\n")
        .parse_game());
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
    const state checking_position(*checking_game);
    const full_move checking_move("(0T1)a1a4");
    state after_check = checking_position;
    assert(after_check.apply_move(checking_move));
    const state checking_view = after_check.phantom(!checking_position.get_present().second);
    assert(checking_view.find_checks(checking_position.get_present().second).first());
    assert(checking_move.pgn(checking_position, QUEEN_W, pgn_options::SHOW_MATE)
               .find('+') != std::string::npos);
    const action checking_action = action::from_vector(
        {ext_move(checking_move, checking_position)}, checking_position);
    assert(checking_action.pgn(checking_position, pgn_options::SHOW_MATE)
               .find('+') != std::string::npos);

    assert(e4.pgn(standard, pgn_options::SHOW_OUTCOME)
        == e4.pgn(standard, pgn_options::SHOW_NOTHING));
    const auto basic = e4.pgn_advanced(standard, pgn_options::SHOW_NOTHING, e5);
    assert(!basic.second.has_value());
    const auto advanced = e4.pgn_advanced(standard, pgn_options::SHOW_MATE, e5);
    assert(advanced.second == mate_type::NONE);
    assert(e4.pgn(standard, pgn_options::SHOW_MATE)
        == e4.pgn_advanced(standard, pgn_options::SHOW_MATE).first);
}
