#undef NDEBUG
#include <cassert>
#include <random>
#include <set>
#include "check_position.h"

namespace {
void compare(const check_position& view, const state& expected)
{
    for (bool c : {false,true}) {
        std::set<full_move> reference;
        for (full_move mv : expected.find_checks(c)) reference.insert(mv);
        const auto moves = view.checks(c);
        const std::set<full_move> actual(moves.begin(),moves.end());
        if (actual != reference) {
            std::cerr << "color " << c << '\n' << expected.to_string();
            for (auto mv : reference) if (!actual.contains(mv)) std::cerr << "missing " << mv << '\n';
            for (auto mv : actual) if (!reference.contains(mv)) std::cerr << "extra " << mv << '\n';
            std::abort();
        }
        const auto first = view.first_check(c);
        assert(first.has_value() == !reference.empty());
        if (first) assert(reference.contains(*first));
        std::set<full_move> sp;
        for (auto mv : reference) if (mv.from.tl() != mv.to.tl()) sp.insert(mv);
        const auto sp_moves = view.checks(c,false);
        assert(std::set<full_move>(sp_moves.begin(),sp_moves.end()) == sp);
        const auto first_sp = view.first_check(c,false);
        assert(first_sp.has_value() == !sp.empty());
        if (first_sp) assert(sp.contains(*first_sp));
    }
}

void compare_phantom(const state& s)
{
    const auto before = s.get_boards();
    const state expected = s.phantom();
    const auto view = check_position::for_phantom(s);
    compare(view,expected);
    const auto [lo, hi] = s.get_lines_range();
    for (int l = lo; l <= hi; ++l) {
        const auto start = s.get_timeline_start(l);
        const auto [t, c] = s.get_timeline_end(l);
        const auto end = expected.get_timeline_end(l);
        for (turn_t at = start; at <= end; at = next_turn(at)) {
            const board* actual = view.board_at(l,at.first,at.second);
            assert(actual);
            assert(actual->get_fen<true>() ==
                   expected.get_board_ptr(l,at.first,at.second)->get_fen<true>());
            const board* borrowed = at <= turn_t{t,c}
                ? s.get_board_ptr(l,at.first,at.second) : s.get_board_ptr(l,t,c);
            assert(actual == borrowed);
        }
        const auto after_end = next_turn(end);
        assert(!view.board_at(l,after_end.first,after_end.second));
    }
    const bool checked = expected.find_checks(!s.get_present().second).first().has_value();
    assert(s.has_phantom_check() == checked);
    assert(s.get_boards() == before);
}

std::shared_ptr<board> random_board(std::mt19937& rng, int width, int height)
{
    auto b = std::make_shared<board>(std::to_string(width),width,height);
    constexpr piece_t pieces[] = {KING_W, ROYAL_QUEEN_W, COMMON_KING_W,
        QUEEN_W, PRINCESS_W, ROOK_W, BISHOP_W, UNICORN_W, DRAGON_W,
        KNIGHT_W, PAWN_W, BRAWN_W, PAWN_UW, BRAWN_UW, KING_UW, ROOK_UW};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        piece_t p = NO_PIECE;
        if (rng()%5 == 0) {
            p = pieces[rng()%std::size(pieces)];
            if (rng()%2) p = to_black(p);
        }
        b->set_piece(ppos(x,y),p);
    }
    return b;
}

void randomized_differential()
{
    std::mt19937 rng(55332);
    for (int trial = 0; trial < 120; ++trial) {
        const int width = trial%2 ? 8 : 5, height = trial%3 ? 8 : 6;
        std::vector<boards_info_t> boards;
        for (int l = -2; l <= 2; ++l) {
            const int begin = rng()%5, end = begin+2+rng()%12;
            for (int ply = begin; ply <= end; ++ply)
                boards.emplace_back(l,ply/2,ply%2,random_board(rng,width,height)->get_fen<true>());
        }
        multiverse_odd original(boards,width,height);
        state s(original);
        check_position view(s,-3,3);
        compare(view,s);
        compare_phantom(s);

        // Simultaneously add boards on several existing lines, and branches
        // on both sides. The independently materialized position is the oracle.
        auto materialized = original.clone();
        std::vector<std::shared_ptr<board>> additions;
        for (int l = -3; l <= 3; ++l) {
            if (l != -3 && l != 3 && rng()%2) continue;
            auto b = random_board(rng,width,height);
            const turn_t at = (l == -3 || l == 3)
                ? turn_t{static_cast<int>(rng()%8),static_cast<bool>(rng()%2)}
                : next_turn(s.get_timeline_end(l));
            view.add_board(l,at,*b);
            materialized->insert_board(l,at.first,at.second,b);
            additions.push_back(std::move(b));
        }
        state expected(*materialized);
        compare(view,expected);
        compare_phantom(expected);
    }
}

void long_rays_and_blockers()
{
    const std::string empty = "8/8/8/8/8/8/8/8";
    for (bool blocked : {false,true}) {
        std::vector<boards_info_t> boards;
        for (int ply = 0; ply <= 20; ++ply) {
            board b(empty);
            if (ply == 0) b.set_piece(ppos(3,3),KING_B);
            if (ply == 20) b.set_piece(ppos(3,3),ROOK_W);
            if (blocked && ply == 10) b.set_piece(ppos(3,3),PAWN_B);
            boards.emplace_back(0,ply/2,ply%2,b.get_fen<true>());
        }
        multiverse_odd m(boards);
        state s(m);
        check_position view(s);
        compare(view,s);
        assert(view.first_check(false,false).has_value() == !blocked);
    }

    // A missing intermediate board stops a slider, even if a farther board
    // exists. Adding that intermediate board must immediately open the ray.
    board attacker(empty), target(empty), bridge(empty);
    attacker.set_piece(ppos(3,3),ROOK_W);
    target.set_piece(ppos(3,3),KING_B);
    multiverse_odd m({{0,3,false,attacker.get_fen<true>()},
                      {1,2,true,empty}, {2,3,false,target.get_fen<true>()}});
    state s(m);
    check_position view(s);
    compare(view,s);
    assert(!view.first_check(false,false));
    view.add_board(1,{3,false},bridge);
    auto expected = m.clone();
    expected->append_board(1,std::make_shared<board>(bridge));
    compare(view,state(*expected));
    assert(view.first_check(false,false));
}

void new_boards_check_each_other()
{
    for (bool mover : {false,true}) {
        multiverse_odd original({{0,2,mover,"8/8/8/8/8/8/8/8"}});
        state s(original);
        check_position view(s,-1,1);
        auto attacker = std::make_shared<board>("8/8/8/8/8/8/8/8");
        auto target = std::make_shared<board>(*attacker);
        attacker->set_piece(ppos(3,3),mover ? ROOK_W : ROOK_B);
        target->set_piece(ppos(3,3),mover ? KING_B : KING_W);
        const auto at = next_turn({2,mover});
        view.add_board(0,at,*attacker);
        view.add_board(1,at,*target);
        auto expected = original.clone();
        expected->append_board(0,attacker);
        expected->insert_board(1,at.first,at.second,target);
        compare(view,state(*expected));
        assert(view.first_check(!mover,false));
        // A branch has no inherited history and uncreated reserved lines are absent.
        assert(!view.board_at(1,2,mover));
        assert(!view.board_at(-1,at.first,at.second));
    }
}
}

int main()
{
    new_boards_check_each_other();
    randomized_differential();
    long_rays_and_blockers();
}
