#undef NDEBUG
#include <cassert>
#include <cctype>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "action.h"
#include "hypercuboid.h"
#include "state.h"

namespace
{
constexpr int present_t = 3;
const std::string walls = "###/###/###";
// A king at a1 is checked by the rook at a3. Its only physical destination,
// a2, remains on that rook's ray. Walls close the other files; escape requires
// an arrival on a historical board with an open landing square. checked_c is
// the same arrangement in file c, keeping the two kings' escapes separate.
const std::string checked_a = "r##/1##/K##";
const std::string checked_c = "##r/##1/##K";
const std::string landing_a = "###/###/1##";
const std::string landing_c = "###/###/##1";

struct fixture
{
    timeline_layout layout;
    bool black;
    std::vector<boards_info_t> boards;

    int line(int white_line) const
    {
        return black ? layout.mirror(white_line) : white_line;
    }

    turn phase(turn white_phase) const
    {
        // Shift the whole phase sequence one half-turn when exchanging colors.
        return turn::from_index(white_phase.index() + static_cast<int>(black));
    }

    void endpoint(int l, turn at, std::string fen)
    {
        if(black)
        {
            for(char &c : fen)
            {
                const unsigned char ch = static_cast<unsigned char>(c);
                if(std::isupper(ch)) c = static_cast<char>(std::tolower(ch));
                else if(std::islower(ch)) c = static_cast<char>(std::toupper(ch));
            }
        }
        const turn transformed = phase(at);
        boards.emplace_back(line(l), transformed.t(), transformed.c(), std::move(fen));
    }

    void history(int l, int landing_t, const std::string &fen)
    {
        // Exactly one historical board accepts an arrival. The endpoint has
        // the mover's side, but its walls provide no nonbranching escape.
        for(turn at{landing_t, false}; at <= turn{present_t + 1, false}; ++at)
            endpoint(l, at, at == turn{landing_t, false} ? fen : walls);
    }

    full_move move(int from_l, int from_t, int from_y,
                   int to_l, int to_t, int to_y, int x = 0) const
    {
        return full_move(vec4(x, from_y, phase(turn{from_t, false}).t(), line(from_l)),
                         vec4(x, to_y, phase(turn{to_t, false}).t(), line(to_l)));
    }

    state position() const
    {
        // Exercise the checked importer as well as the geometric construction.
        std::ostringstream pgn;
        pgn << "[Board \"Custom\"]\n[Size \"3x3\"]\n[Timeline \""
            << (layout.is_even() ? "even" : "odd") << "\"]\n";
        for(const auto &[l, t, c, fen] : boards)
            pgn << '[' << fen << ':' << layout.pretty_l(l) << ':' << t
                << ':' << (c ? 'b' : 'w') << "]\n";
        return state::from_pgn(pgn.str());
    }
};

struct action_counts
{
    size_t all = 0;
    size_t forward = 0;
};

action_counts verify(const fixture &f, mate_type expected,
                     const moveseq &witness, bool witness_forward,
                     bool expected_check, bool expected_forward)
{
    const state initial = f.position();
    const turn original = f.phase(turn{present_t, false});
    assert(initial.get_present() == original);
    assert(initial.apparent_present() == original);
    assert(initial.has_phantom_check() == expected_check);

    std::optional<action> witness_action;
    if(!witness.empty())
    {
        witness_action = action::from_moveseq(witness, initial);
        const auto result = initial.can_apply(*witness_action);
        assert(result);
        assert(result->apparent_present() != original);
        assert((result->apparent_present() > original) == witness_forward);
    }

    // Reference classification uses the actual submitted Present, without
    // get_mate_type's branch cap, active-branch count, or destination filter.
    // Enumeration still assumes correctness of the underlying action search.
    auto [info, space] = HC_info::build_HC(initial);
    action_counts counts;
    std::set<action> actions;
    for(const moveseq &moves : info.search<natural_HC_ordering, hc_policy::iterative>(std::move(space)))
    {
        const action act = action::from_moveseq(moves, initial);
        assert(actions.insert(act).second);
        const auto result = initial.can_apply(act);
        assert(result);
        assert(result->apparent_present() != original);
        ++counts.all;
        if(result->apparent_present() > original) ++counts.forward;
        // These fixtures are deliberately small; unexpected growth is a failure.
        assert(counts.all <= 256);
    }
    assert((counts.forward != 0) == expected_forward);
    if(witness_action) assert(actions.contains(*witness_action));
    else assert(counts.all == 0);

    mate_type reference = mate_type::NONE;
    if(counts.all == 0)
        reference = expected_check ? mate_type::CHECKMATE : mate_type::STALEMATE;
    else if(expected_check && counts.forward == 0)
        reference = mate_type::SOFTMATE;
    assert(reference == expected);
    assert(initial.get_mate_type() == expected);
    if(counts.all != 0) assert(initial.get_mate_type(true) == expected);
    return counts;
}

fixture single_escape(timeline_layout layout, bool black, turn reactivated,
                      bool playable = false, int landing_t = present_t,
                      bool check = true)
{
    fixture f{layout, black, {}};
    const int source = layout.initial_range().first;
    const int target = source - 1;
    f.endpoint(source, turn{present_t, false}, check ? checked_a : "###/###/K##");
    if(layout.is_even()) f.endpoint(0, turn{present_t, true}, walls);
    f.history(target, landing_t, landing_a);
    f.endpoint(layout.mirror(2), reactivated, playable ? "###/1##/C##" : walls);
    return f;
}

void test_reactivation(timeline_layout layout, bool black)
{
    const int source = layout.initial_range().first;
    const auto escape = [&](const fixture &f) {
        return moveseq{f.move(source, present_t, 0, source - 1, present_t, 0)};
    };
    // The same arrival goes forward unless activating an older endpoint pulls
    // the Present backward. Neither position permits a nonbranching escape.
    const fixture earlier = single_escape(layout, black, turn{present_t - 1, true});
    assert(verify(earlier, mate_type::SOFTMATE, escape(earlier), false, true, false).all == 1);
    const fixture later = single_escape(layout, black, turn{present_t, true});
    assert(verify(later, mate_type::NONE, escape(later), true, true, true).all == 1);

    // An older mover-sided endpoint advances once, but remains a half-turn
    // before the original Present. It cannot catch up during this action.
    const fixture playable = single_escape(layout, black, turn{present_t - 1, false}, true);
    moveseq witness{playable.move(layout.mirror(2), present_t - 1, 0,
                                 layout.mirror(2), present_t - 1, 1)};
    witness.push_back(escape(playable).front());
    verify(playable, mate_type::SOFTMATE, witness, false, true, false);

    // Equality is safe: a reactivated endpoint at the original Present can
    // advance to the opponent's phase in this same action.
    const fixture same_phase = single_escape(layout, black, turn{present_t, false}, true);
    witness = {same_phase.move(layout.mirror(2), present_t, 0,
                               layout.mirror(2), present_t, 1)};
    witness.push_back(escape(same_phase).front());
    verify(same_phase, mate_type::NONE, witness, true, true, true);

    // A past arrival on the first active new timeline itself forces retreat,
    // even when every reactivated existing endpoint is safe.
    const fixture past = single_escape(layout, black, turn{present_t, true},
                                      false, present_t - 1);
    witness = {past.move(source, present_t, 0, source - 1, present_t - 1, 0)};
    assert(verify(past, mate_type::SOFTMATE, witness, false, true, false).all == 1);

    // With no check, even exclusively backward actions do not mean softmate.
    const fixture unchecked = single_escape(layout, black, turn{present_t - 1, true},
                                            false, present_t, false);
    verify(unchecked, mate_type::NONE, escape(unchecked), false, false, false);
}

fixture two_escapes(timeline_layout layout, bool black, bool past_second)
{
    fixture f{layout, black, {}};
    const int created = past_second ? 2 : 1;
    const int opponent_created = past_second ? 2 : 5;
    const auto [central_min, central_max] = layout.initial_range();
    for(int l = central_min - opponent_created; l <= central_max + created; ++l)
    {
        if(l == 0) f.endpoint(l, turn{present_t, false}, checked_a);
        else if(l == -1) f.endpoint(l, turn{present_t, false}, checked_c);
        else if(l == 1) f.history(l, present_t, landing_a);
        else if(l == -2) f.history(l, past_second ? present_t - 1 : present_t, landing_c);
        else f.endpoint(l, turn{present_t, true}, walls);
    }
    return f;
}

void test_reactivation_prefix(timeline_layout layout, bool black)
{
    fixture f = two_escapes(layout, black, false);
    // The inactive prefix is [safe, earlier, safe]. Both checked kings must
    // leave, requiring two branches and activating the intervening old board.
    const int unsafe_line = f.line(layout.mirror(4));
    for(auto &[l, t, c, fen] : f.boards)
        if(l == unsafe_line)
        {
            const turn earlier = f.phase(turn{present_t - 1, true});
            t = earlier.t();
            c = earlier.c();
        }
    const moveseq witness{f.move(0, present_t, 0, 1, present_t, 0),
                          f.move(-1, present_t, 0, -2, present_t, 0, 2)};
    verify(f, mate_type::SOFTMATE, witness, false, true, false);
}

void test_inactive_arrival(timeline_layout layout, bool black)
{
    const fixture f = two_escapes(layout, black, true);
    // Equal created counts: only the first new timeline is active. The second
    // king travels into the past on an inactive branch, allowing a forward turn.
    const moveseq witness{f.move(0, present_t, 0, 1, present_t, 0),
                          f.move(-1, present_t, 0, -2, present_t - 1, 0, 2)};
    verify(f, mate_type::NONE, witness, true, true, true);

    // The mover is already one timeline ahead: no new timeline is active,
    // including the very first branch. Its arrival may therefore be in the past.
    fixture none_active{layout, black, {}};
    none_active.endpoint(0, turn{present_t, false}, checked_a);
    if(layout.is_even()) none_active.endpoint(-1, turn{present_t, true}, walls);
    none_active.history(1, present_t - 1, landing_a);
    const moveseq inactive_witness{
        none_active.move(0, present_t, 0, 1, present_t - 1, 0)};
    assert(verify(none_active, mate_type::NONE, inactive_witness,
                  true, true, true).all == 1);
}

void test_no_escape(timeline_layout layout, bool black)
{
    for(bool check : {false, true})
    {
        fixture f{layout, black, {}};
        f.endpoint(0, turn{present_t, false}, check ? checked_a : "###/###/K##");
        if(layout.is_even()) f.endpoint(-1, turn{present_t, true}, walls);
        verify(f, check ? mate_type::CHECKMATE : mate_type::STALEMATE,
               {}, false, check, false);
    }
}
} /* anonymous namespace */

int main()
{
    for(const auto layout : {timeline_layout::odd, timeline_layout::even})
        for(bool black : {false, true})
        {
            test_reactivation(layout, black);
            test_reactivation_prefix(layout, black);
            test_inactive_arrival(layout, black);
            test_no_escape(layout, black);
        }
    std::cerr << "test_softmate: all passed\n";
}
