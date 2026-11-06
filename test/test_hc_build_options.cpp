#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "check_position.h"
#include "finetree.h"

namespace
{
index_t physical_coordinate(const HC_info &info, index_t axis, const full_move &move)
{
    for(index_t i : info.get_universe()[axis])
    {
        const auto semimove = info.get_semimove(axis, i);
        if(const auto *physical = semimove.get_if<physical_move>();
           physical && physical->m == move) return i;
    }
    assert(false && "missing physical coordinate");
    return 0;
}

std::set<moveseq> reference_actions(const state &position)
{
    auto [info, initial] = HC_info::build_HC(position);
    std::set<moveseq> result;
    for(auto moves : info.search<natural_HC_ordering, hc_policy::stable>(std::move(initial)))
        assert(result.insert(std::move(moves)).second);
    return result;
}

template<HCOrdering Order>
void compare_actions(const state &position, const std::set<moveseq> &expected,
                     const HC_info &info, search_space space, const Order &order)
{
    for(const HC &region : space)
        for(index_t n = 0; n < info.dimension(); ++n)
            assert(region[n].is_subset_of(info.get_universe()[n]));
    std::set<moveseq> actual;
    for(auto moves : info.search<Order, hc_policy::stable>(std::move(space), order))
    {
        assert(position.can_apply(action::from_moveseq(moves, position)));
        assert(actual.insert(std::move(moves)).second);
    }
    assert(actual == expected);
}

void compare_position(const state &position)
{
    const auto before = position.to_string();
    const auto expected = reference_actions(position);
    auto [info, space] = HC_info::build_HC(position, {.remove_check = true});
    compare_actions(position, expected, info, space, natural_HC_ordering{});
    std::mt19937 rng(42);
    compare_actions(position, expected, info, space, random_HC_ordering(info.get_universe(), rng));
    assert(position.to_string() == before);
}

void branch_partitions_exclude_gaps()
{
    const auto position = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n");
    auto [info, space] = HC_info::build_HC(position, {.remove_check = true});
    const HC &domain = info.get_universe();
    const index_t first = info.first_branch_axis();
    assert(info.dimension() - first >= 2);
    const auto visit = [&](const auto &self, index_t n, point &p) -> void {
        if(n < info.dimension())
        {
            const auto &values = n < first ? integer_set{*domain[n].begin()} : domain[n];
            for(index_t i : values) {p[n] = i; self(self, n + 1, p);}
            return;
        }
        bool seen_null = false, gap = false;
        for(index_t axis = first; axis < info.dimension(); ++axis)
        {
            if(p[axis] == 0) seen_null = true;
            else if(seen_null) gap = true;
        }
        assert(space.contains(p) == !gap);
    };
    point p(info.dimension());
    visit(visit, 0, p);
    compare_position(position);
}

void unavoidable_history_checks()
{
    for(bool player : {false, true})
    {
        const std::string old_board = player ? "3K/4/4/1k2" : "3k/4/4/1K2";
        const std::string current = player ? "3K/4/3n/1R1k" : "3k/4/3N/1r1K";
        std::vector<boards_info_t> boards{{0, 1, false, old_board}, {0, 1, true, old_board}};
        if(player) boards.emplace_back(0, 2, false, old_board);
        boards.emplace_back(0, 2, player, current);
        const state position(multiverse(timeline_layout::odd, boards, 4, 4));
        auto [regular, initial] = HC_info::build_HC(position);
        auto [checked, space] = HC_info::build_HC(position, {.remove_check = true});
        const HC &domain = checked.get_universe();
        const index_t axis = *regular.line_to_axis(0);
        const index_t exposed = physical_coordinate(regular, axis, full_move("(0T2)d1c2"));
        assert(!domain[axis].contains(exposed));
        // Removing the current king also exposes the historical king to the
        // rook. All corresponding arrivals must lose their departure support.
        bool removed_departure = false;
        for(index_t i : regular.get_universe()[axis])
        {
            const auto semimove = regular.get_semimove(axis, i);
            if(const auto *departure = semimove.get_if<departing_move>();
               departure && departure->from == vec4(3, 0, 2, 0))
            {
                removed_departure = true;
                assert(!domain[axis].contains(i));
            }
        }
        assert(removed_departure);
        const index_t safe = physical_coordinate(regular, axis, full_move("(0T2)d2b1"));
        assert(domain[axis].contains(safe));
        const auto safe_move = checked.get_semimove(axis, safe);
        assert(safe_move.get<physical_move>().m == full_move("(0T2)d2b1"));
        compare_position(position);
    }
}

void conditional_checks_stay_available()
{
    const auto position = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/Kr2:0:1:w]\n[3k/4/4/K3:1:1:w]\n");
    auto [info, space] = HC_info::build_HC(position, {.remove_check = true});
    const HC &domain = info.get_universe();
    const index_t first = *info.line_to_axis(0), second = *info.line_to_axis(1);
    const index_t exposed = physical_coordinate(info, first, full_move("(0T1)a1a2"));
    const index_t into_check = physical_coordinate(info, second, full_move("(1T1)a1b1"));
    const index_t safe = physical_coordinate(info, second, full_move("(1T1)a1a2"));
    point p(info.dimension(), 0);
    p[first] = exposed; p[second] = into_check;
    assert(info.find_checks(p, domain));
    p[second] = safe;
    assert(!info.find_problem(p, domain));
    compare_position(position);
}

void optional_move_pruning()
{
    for(bool player : {false, true})
    {
        const std::string board_text = player ? "kn2/4/4/3K" : "3k/4/4/KN2";
        const state position(multiverse(timeline_layout::odd,
            {{0, 3, player, board_text}, {1, 4, player, board_text}, {2, 4, player, board_text}}, 4, 4));
        const auto [mandatory, optional, unplayable] = position.get_timeline_status();
        assert(!mandatory.empty() && !optional.empty());
        auto expected = reference_actions(position);
        std::erase_if(expected, [&](const moveseq &moves) {
            state partial = position;
            for(const full_move &move : moves)
            {
                const auto is_optional = [&](int line) {
                    return std::find(optional.begin(), optional.end(), line) != optional.end();
                };
                if(is_optional(move.from.l()))
                {
                    if(move.from.tl() == move.to.tl()) return true;
                    if(is_optional(move.to.l())
                       && partial.get_timeline_end(move.to.l()) == turn{move.to.t(), player})
                        return true;
                }
                assert(partial.apply_move<true>(move));
            }
            return false;
        });
        assert(!expected.empty());
        assert(expected.size() < reference_actions(position).size());
        auto [pruned, space] = HC_info::build_HC(position, {.prune_optional_moves = true});
        for(int line : optional)
        {
            const auto axis = *pruned.line_to_axis(line);
            for(index_t i : pruned.get_universe()[axis])
            {
                const auto semi = pruned.get_semimove(axis, i);
                assert(!semi.is<physical_move>());
                if(const auto *arrival = semi.get_if<arriving_move>())
                    assert(std::find(optional.begin(), optional.end(), arrival->m.from.l()) == optional.end());
            }
        }
        std::set<moveseq> actual;
        for(auto moves : pruned.search<natural_HC_ordering, hc_policy::stable>(space))
        {
            assert(position.can_apply(action::from_moveseq(moves, position)));
            actual.insert(std::move(moves));
        }
        assert(actual == expected);
        auto [checked, checked_space] = HC_info::build_HC(position,
            {.remove_check = true, .prune_optional_moves = true});
        compare_actions(position, expected, checked, checked_space, natural_HC_ordering{});
        for(HC_build_options options : {
            HC_build_options{.prune_optional_moves = true},
            HC_build_options{.remove_check = true, .prune_optional_moves = true}})
        {
            auto root = fine_node<>::make_root(position, {}, options);
            std::set<moveseq> tree_actions;
            const auto visit = [&](const auto &self, fine_node<> *node) -> void {
                if(node->is_ceiling()) { tree_actions.insert(node->to_action()); return; }
                for([[maybe_unused]] index_t i : node->search(natural_HC_ordering{})) {}
                for(auto *child : node->get_children()) self(self, child);
            };
            visit(visit, root.get());
            assert(tree_actions == expected);
            auto *ceiling = finetree_nearby_ceiling(root.get());
            assert(ceiling);
            const auto next = position.can_apply(action::from_moveseq(ceiling->to_action(), position));
            assert(next);
            ceiling->ignite();
            assert(ceiling->is_terminal() == reference_actions(*next).empty());
        }
    }
}

void pruning_falls_back_when_a_reply_requires_optional_moves()
{
    const auto position = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[2N1/4/1rbR/Kk2:0:2:w]\n[2kb/2N1/1K2/2rR:1:3:w]\n");
    auto [pruned, space] = HC_info::build_HC(position, {.prune_optional_moves = true});
    assert(!pruned.search(space).first());
    const auto expected = reference_actions(position);
    assert(!expected.empty());
    for(bool remove_check : {false, true})
    {
        auto root = fine_node<>::make_root(position, {},
            HC_build_options{.remove_check = remove_check, .prune_optional_moves = true});
        assert(!root->is_terminal());
        const auto *reply = finetree_nearby_ceiling(root.get());
        assert(reply && expected.contains(reply->to_action()));
        assert(position.can_apply(action::from_moveseq(reply->to_action(), position)));
    }
}

void overlay_restoration()
{
    const auto position = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n[3k/4/4/KN2:0:1:w]\n");
    const board result("3k/4/1N2/K3", 4, 4);
    check_position view(position, -1, 1);
    const auto before = view.checks(true);
    assert(!view.get_board_ptr(0, 1, true));
    (void)view.first_check(true, check_position::board_overlay{0, 1, true, &result});
    assert(!view.get_board_ptr(0, 1, true));
    assert(view.checks(true) == before);
    const board branch("3k/4/K3/r3", 4, 4);
    const auto check = view.first_check(true, check_position::board_overlay{1, 1, true, &branch},
        check_position::board_overlay{0, 1, true, &result}, false);
    assert(check && check->from.l() == 1 && check->to.l() == 0);
    assert(!view.get_board_ptr(1, 1, true));
    assert(!view.get_board_ptr(0, 1, true));
    assert(view.checks(true) == before);
}

void departure_arrival_support()
{
    // Reverse colors, turn parity and timeline direction to cover both players
    // without changing the geometry of the branching moves.
    const auto position_for = [](std::vector<boards_info_t> boards, bool reverse) {
        if(reverse)
            for(auto &[line, time, color, fen] : boards)
            {
                line = -line;
                time += color;
                color = !color;
                for(char &piece : fen)
                    if(piece >= 'a' && piece <= 'z') piece += 'A' - 'a';
                    else if(piece >= 'A' && piece <= 'Z') piece += 'a' - 'A';
            }
        return state(multiverse(timeline_layout::odd, std::move(boards), 4, 4));
    };
    for(bool reverse : {false, true})
    {
        const auto position = position_for({
            {0, 1, false, "n2B/2b1/Nkr1/RK2"},
            {0, 1, true, "n2B/2b1/Nkr1/RK2"},
            {0, 2, false, "n2B/2b1/Nkr1/RK2"},
            {1, 1, false, "k1Br/1N1b/KR2/3n"},
            {1, 1, true, "k1Br/1N1b/KR2/3n"},
            {1, 2, false, "k1Br/1N1b/KR2/3n"}}, reverse);
        auto [regular, initial] = HC_info::build_HC(position);
        auto [checked, space] = HC_info::build_HC(position, {.remove_check = true});
        const int second_line = reverse ? -1 : 1;
        const auto verify = [&](vec4 from, bool retained, int arrivals, int survivors) {
            const index_t source = *regular.line_to_axis(from.l());
            bool found = false;
            int total = 0, remaining = 0;
            for(index_t n = 0; n < regular.dimension(); ++n)
                for(index_t i : regular.get_universe()[n])
                {
                    const auto semimove = regular.get_semimove(n, i);
                    if(const auto *departure = semimove.get_if<departing_move>();
                       departure && departure->from == from)
                    {
                        assert(n == source);
                        assert(checked.get_universe()[n].contains(i) == retained);
                        found = true;
                    }
                    if(const auto *arrival = semimove.get_if<arriving_move>();
                       arrival && arrival->m.from == from)
                    {
                        ++total;
                        remaining += checked.get_universe()[n].contains(i);
                    }
                }
            assert(found && total == arrivals && remaining == survivors);
        };
        // The departure itself creates a historical check: all arrivals fail.
        verify(vec4(1, 0, 2, 0), false, 10, 0);
        // Rejected arrivals must not erase support from a surviving arrival.
        verify(vec4(1, 1, 2, second_line), true, 3, 1);
        // Both surviving arrivals are copies on distinct branching axes.
        verify(vec4(0, 1, 2, second_line), true, 2, 2);
        compare_position(position);

        const auto depleted = position_for({
            {0, 1, true, "4/1n1b/1NrK/1kBR"},
            {1, 1, true, "1Rn1/2r1/B1Kk/2Nb"}}, reverse);
        auto [full, full_space] = HC_info::build_HC(depleted);
        auto [pruned, pruned_space] = HC_info::build_HC(depleted, {.remove_check = true});
        const int time = reverse ? 2 : 1;
        const bool player = depleted.get_present().c();
        const vec4 from(1, 0, time, 0);
        int arrivals = 0;
        bool found = false;
        for(index_t n = 0; n < full.dimension(); ++n)
            for(index_t i : full.get_universe()[n])
            {
                const auto semimove = full.get_semimove(n, i);
                if(const auto *departure = semimove.get_if<departing_move>();
                   departure && departure->from == from)
                {
                    assert(!pruned.get_universe()[n].contains(i));
                    found = true;
                }
                if(const auto *arrival = semimove.get_if<arriving_move>();
                   arrival && arrival->m.from == from)
                {
                    const auto boards = *full.get_move_boards(n, i);
                    const turn at = turn{time, player}.next_turn();
                    check_position view(depleted);
                    assert(!view.first_check(!player,
                        check_position::board_overlay{0, at.t(), at.c(), boards.departure},
                        std::nullopt, false));
                    assert(!pruned.get_universe()[n].contains(i));
                    ++arrivals;
                }
            }
        // The standalone departure is check-free, but none of its arrivals is.
        assert(found && arrivals == 3);
        compare_position(depleted);
    }
}
} /* anonymous namespace */

int main()
{
    optional_move_pruning();
    pruning_falls_back_when_a_reply_requires_optional_moves();
    branch_partitions_exclude_gaps();
    unavoidable_history_checks();
    conditional_checks_stay_available();
    overlay_restoration();
    departure_arrival_support();
    compare_position(state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n[4/4/rk2/K3:0:1:w]\n"));
    const auto fixed_check = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/K3/n3:0:2:b]\n[3k/4/4/K3:1:0:b]\n[3k/4/4/3K:1:1:w]\n");
    assert(!fixed_check.get_present().c());
    assert(check_position(fixed_check).first_check(true, false));
    auto [fixed_info, fixed_space] = HC_info::build_HC(fixed_check, {.remove_check = true});
    assert(fixed_info.get_universe().empty());
    compare_position(fixed_check);
    for(int turn : {3, 5})
        compare_position(state::from_pgn<true>(
            "[Board \"custom\"]\n[Size \"4x4\"]\n[3k/4/4/KN2:-2:" + std::to_string(turn)
            + ":b]\n[3k/4/4/KN2:-1:5:w]\n[3k/4/4/KN2:-1:5:b]\n"
              "[3k/4/4/KN2:-1:6:w]\n[3k/4/4/KN2:0:5:w]\n"));
}
