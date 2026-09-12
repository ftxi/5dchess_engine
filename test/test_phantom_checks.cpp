#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>
#include "check_position.h"
#include "pgnparser.h"

namespace {
constexpr const char* empty = "8/8/8/8/8/8/8/8";

state cross_line_position(bool mover)
{
    board source(empty), target(empty);
    source.set_piece(ppos(3,3),mover ? ROOK_W : ROOK_B);
    target.set_piece(ppos(3,3),mover ? KING_B : KING_W);
    multiverse_odd m({{0,1,mover,source.get_fen<true>()},
                      {1,1,mover,target.get_fen<true>()}});
    return state(m);
}

state physical_position(bool mover)
{
    board b(empty);
    b.set_piece(ppos(3,3),mover ? KING_B : KING_W);
    b.set_piece(ppos(3,6),mover ? ROOK_W : ROOK_B);
    multiverse_odd m({{0,1,mover,b.get_fen<true>()}});
    return state(m);
}

void compare_phantom(const state& s)
{
    const auto [t, player] = s.get_present();
    (void)t;
    const state expected = s.phantom();
    const auto view = check_position::for_phantom(s);
    std::set<full_move> reference, actual;
    for (auto mv : expected.find_checks(!player)) reference.insert(mv);
    for (auto mv : view.checks(!player)) actual.insert(mv);
    assert(reference == actual);
    assert(s.has_phantom_check() == !reference.empty());
    const auto [lo, hi] = s.get_lines_range();
    for (int l = lo; l <= hi; ++l) {
        const auto end = expected.get_timeline_end(l);
        for (auto at = s.get_timeline_start(l); at <= end; at = next_turn(at)) {
            const auto* b = view.board_at(l,at.first,at.second);
            assert(b);
            assert(b->get_fen<true>() == expected.get_board_ptr(l,at.first,at.second)->get_fen<true>());
        }
        const auto after = next_turn(end);
        assert(!view.board_at(l,after.first,after.second));
    }
}

void test_phantom_cases()
{
    for (bool mover : {false,true}) {
        const state physical = physical_position(mover);
        compare_phantom(physical);
        assert(physical.has_phantom_check());
        const state cross = cross_line_position(mover);
        compare_phantom(cross);
        const auto view = check_position::for_phantom(cross);
        const auto hit = view.first_check(!mover,false);
        assert(hit && hit->from.l() != hit->to.l());
        const auto at = next_turn({1,mover});
        assert(hit->from.t() == at.first && hit->to.t() == at.first);

        // During an action the stored player must be used, even after the
        // apparent present has changed to the opponent before submit().
        const std::string fen = mover ? "8/8/8/8/8/8/8/r7" : "8/8/8/8/8/8/8/R7";
        multiverse_odd m({{0,1,mover,fen}, {1,1,mover,fen}});
        state partial(m);
        compare_phantom(partial);
        assert(partial.apply_move<true>(full_move(vec4(0,0,1,0),vec4(0,1,1,0))));
        compare_phantom(partial);
        assert(partial.apply_move<true>(full_move(vec4(0,0,1,1),vec4(0,1,1,1))));
        assert(partial.get_present().second != partial.apparent_present().second);
        compare_phantom(partial);
        const auto before_submit = check_position::for_phantom(partial);
        // Neither endpoint matches the stored player: append nothing.
        for (int l : {0,1}) {
            const auto end = partial.get_timeline_end(l);
            const auto after = next_turn(end);
            assert(!before_submit.board_at(l,after.first,after.second));
        }
        assert(partial.submit());
        compare_phantom(partial);

        multiverse_even even({{-1,1,mover,fen}, {0,1,mover,fen}});
        compare_phantom(state(even));
    }
}

state long_history()
{
    std::vector<boards_info_t> boards;
    for (int l = -2; l <= 2; ++l) {
        for (int ply = 0; ply <= 128; ++ply)
            boards.emplace_back(l,ply/2,ply%2,"4k3/8/8/8/8/8/8/4K3");
    }
    multiverse_odd m(boards);
    return state(m);
}

void benchmark()
{
    using clock = std::chrono::steady_clock;
    std::vector<std::pair<std::string,state>> cases;
    cases.emplace_back("standard",state(*pgnparser("[Board \"Standard - Turn Zero\"]").parse_game()));
    cases.emplace_back("645_boards_no_check",long_history());
    cases.emplace_back("physical_check",physical_position(false));
    cases.emplace_back("new_board_cross_line_check",cross_line_position(true));
    constexpr int iterations = 2000, rounds = 7;
    std::cout << "case,iterations,round,clone_us,view_us,checked\n";
    for (const auto& [name,s] : cases) {
        const bool attacker = !s.get_present().second;
        const bool expected = s.phantom().find_checks(attacker).first().has_value();
        auto measure = [&](bool borrowed) {
            int count = 0;
            const auto start = clock::now();
            for (int i = 0; i < iterations; ++i)
                count += borrowed ? s.has_phantom_check()
                    : s.phantom().find_checks(attacker).first().has_value();
            const double us = std::chrono::duration<double,std::micro>(clock::now()-start).count()/iterations;
            assert(count == (expected ? iterations : 0));
            return us;
        };
        // Warm each path before taking measurements; alternate measured order.
        measure(false);
        measure(true);
        for (int round = 0; round < rounds; ++round) {
            double clone_us, view_us;
            if (round%2) { view_us = measure(true); clone_us = measure(false); }
            else { clone_us = measure(false); view_us = measure(true); }
            std::cout << name << ',' << iterations << ',' << round << ','
                      << std::fixed << std::setprecision(6) << clone_us << ','
                      << view_us << ',' << expected << '\n';
        }
    }
}
}

int main(int argc, char** argv)
{
    test_phantom_cases();
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") benchmark();
    else if (argc != 1) return 2;
}
