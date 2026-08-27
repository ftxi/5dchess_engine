#undef NDEBUG
#include <cassert>
#include <stdexcept>
#include <string>
#include "client/game.h"
#include "core/hypercuboid.h"
#include "core/pgnparser.h"
#include "core/variants.h"

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

int main()
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
        catch(const std::runtime_error &)
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
    knight.metadata["promotions"] = "Q";
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
    assert(default_game.show_pgn().contains("[Promotions \"Q\"]"));

    const std::string before_promotion = R"(
[Promotions "Q"]
[Timeline "odd"]
[Size "8x8"]
[5bk1/5N2/7P/6K1/8/8/8/8:0:1:w]

1. h7+ / Kxf7
)";
    const auto parsed = pgnparser(before_promotion).parse_game();
    assert(parsed.has_value());
    const state promotion_state(*parsed);
    const auto [fm, pt, candidates] = promotion_state.parse_move("h8=Q");
    assert(fm.has_value());
    const action implicit_queen = action::from_vector({ext_move(*fm)}, promotion_state);
    const action explicit_queen = action::from_vector(
        {ext_move(*fm, QUEEN_W)}, promotion_state);
    assert(implicit_queen == explicit_queen);
    assert(implicit_queen.get_moves()[0].promote_to == QUEEN_W);
    assert(!promotion_state.can_apply(*fm, KNIGHT_W));
    assert(!promotion_state.can_apply(*fm, KNIGHT_B));

    const std::string no_promotion_before = R"(
[Promotions ""]
[Timeline "odd"]
[Size "8x8"]
[5bk1/5N2/7P/6K1/8/8/8/8:0:1:w]

1. h7+ / Kxf7
)";
    const auto no_promotion_parsed = pgnparser(no_promotion_before).parse_game();
    assert(no_promotion_parsed.has_value());
    const state no_promotion_state(*no_promotion_parsed);
    assert(no_promotion_state.get_promotion_options() == promotion_options::NONE);
    const auto [no_fm, no_pt, no_candidates] = no_promotion_state.parse_move("h8");
    assert(no_fm.has_value());
    assert(!no_pt.has_value());
    const auto unchanged_pawn = no_promotion_state.can_apply(*no_fm);
    assert(unchanged_pawn.has_value());
    const auto [pawn_t, pawn_c] = unchanged_pawn->get_timeline_end(no_fm->to.l());
    assert(unchanged_pawn->get_piece(
        vec4(no_fm->to.x(), no_fm->to.y(), pawn_t, no_fm->to.l()), pawn_c)
        == PAWN_W);
    assert(!no_promotion_state.can_apply(*no_fm, QUEEN_W));

    const action no_promotion_action = action::from_vector(
        {ext_move(*no_fm)}, no_promotion_state);
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
    assert(unsafe_state.apply_move<true>(*fm, KNIGHT_W));
    const auto [end_t, end_c] = unsafe_state.get_timeline_end(fm->to.l());
    assert(unsafe_state.get_piece(
        vec4(fm->to.x(), fm->to.y(), end_t, fm->to.l()), end_c) == KNIGHT_W);

    assert(sizeof(promotion_options) == 1);
    return 0;
}
