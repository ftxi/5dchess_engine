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
void compare_solid(const state &position, const std::set<moveseq> &expected,
                   const HC_info &info, HC domain, std::vector<slice> negative,
                   const Order &order)
{
    HC_info::witness_space<hc_policy::tree> space(search_space{domain});
    space.negative = std::move(negative);
    std::set<moveseq> actual;
    while(auto p = info.find_witness(space, domain, order))
    {
        auto moves = info.to_action(*p);
        assert(position.can_apply(action::from_moveseq(moves, position)));
        assert(actual.insert(std::move(moves)).second);
        slice consumed;
        for(index_t n = 0; n < info.dimension(); ++n) consumed.fix_axis(n, integer_set{(*p)[n]});
        space.positive.remove_slice_globally(consumed);
    }
    assert(actual == expected);
}

void compare_position(const state &position)
{
    const auto before = position.to_string();
    const auto expected = reference_actions(position);
    auto [info, domain, negative] = HC_info::build_solid_HC(position);
    for(index_t n = 0; n < info.dimension(); ++n)
        assert(std::ranges::equal(domain[n], info.get_universe()[n]));
    compare_solid(position, expected, info, domain, negative, natural_HC_ordering{});
    std::mt19937 rng(42);
    compare_solid(position, expected, info, domain, negative, random_HC_ordering(domain, rng));
    assert(position.to_string() == before);
}

void branch_constraints_and_ordering()
{
    const auto position = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n");
    auto [regular, initial] = HC_info::build_HC(position);
    auto [info, domain, negative] = HC_info::build_solid_HC(position);
    const index_t first = info.first_branch_axis();
    assert(info.dimension() - first >= 2);
    assert(negative.size() == info.dimension() - first - 1);
    // Exactly the branch-prefix holes removed by the old partition are negatives.
    const auto visit = [&](const auto &self, index_t n, point &p) -> void {
        if(n < info.dimension())
        {
            const auto &values = n < first ? integer_set{*domain[n].begin()} : domain[n];
            for(index_t i : values) {p[n] = i; self(self, n + 1, p);}
            return;
        }
        const bool excluded = std::any_of(negative.begin(), negative.end(),
            [&](const slice &problem) { return problem.contains(p); });
        assert(initial.contains(p) == !excluded);
        if(excluded)
        {
            auto problem = info.jump_order_consistent(p, domain);
            assert(problem && problem->contains(p));
        }
    };
    point p(info.dimension());
    visit(visit, 0, p);

    // A higher-ranked branching point is available in the one initial HC;
    // the old initial back rectangle categorically excluded every branch.
    HC remaining = domain;
    std::optional<point> preferred;
    HC_info::witness_space<hc_policy::tree> space(search_space{domain});
    space.negative = negative;
    while(auto candidate = info.find_witness(space, remaining))
    {
        if((*candidate)[first] != 0) {preferred = std::move(candidate); break;}
        slice consumed;
        for(index_t n = 0; n < info.dimension(); ++n) consumed.fix_axis(n, integer_set{(*candidate)[n]});
        space.positive.remove_slice_globally(consumed);
    }
    assert(preferred && domain.contains(*preferred));
    assert(!initial.back().contains(*preferred));
    std::vector<std::vector<float>> scores(info.dimension());
    for(index_t n = 0; n < info.dimension(); ++n)
    {
        const auto largest = *std::max_element(domain[n].begin(), domain[n].end());
        scores[n].resize(largest + 1, 0);
        scores[n][(*preferred)[n]] = 1;
    }
    std::mt19937 rng(7);
    scored_HC_ordering order(domain, scores, rng);
    point corner(info.dimension());
    for(index_t n = 0; n < info.dimension(); ++n) corner[n] = *order(n, domain[n]).begin();
    assert(corner == *preferred);
    assert(!info.find_problem(corner, domain));
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
        auto [solid, domain, negative] = HC_info::build_solid_HC(position);
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
        const auto safe_move = solid.get_semimove(axis, safe);
        assert(safe_move.get<physical_move>().m == full_move("(0T2)d2b1"));
        compare_position(position);
    }
}

void conditional_checks_stay_available()
{
    const auto position = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/Kr2:0:1:w]\n[3k/4/4/K3:1:1:w]\n");
    auto [info, domain, negative] = HC_info::build_solid_HC(position);
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
} /* anonymous namespace */

int main()
{
    branch_constraints_and_ordering();
    unavoidable_history_checks();
    conditional_checks_stay_available();
    overlay_restoration();
    compare_position(state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n[4/4/rk2/K3:0:1:w]\n"));
    const auto fixed_check = state::from_pgn<true>(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/K3/n3:0:2:b]\n[3k/4/4/K3:1:0:b]\n[3k/4/4/3K:1:1:w]\n");
    assert(!fixed_check.get_present().c());
    assert(check_position(fixed_check).first_check(true, false));
    auto [fixed_info, fixed_domain, fixed_negative] = HC_info::build_solid_HC(fixed_check);
    assert(fixed_domain.empty());
    compare_position(fixed_check);
    for(int turn : {3, 5})
        compare_position(state::from_pgn<true>(
            "[Board \"custom\"]\n[Size \"4x4\"]\n[3k/4/4/KN2:-2:" + std::to_string(turn)
            + ":b]\n[3k/4/4/KN2:-1:5:w]\n[3k/4/4/KN2:-1:5:b]\n"
              "[3k/4/4/KN2:-1:6:w]\n[3k/4/4/KN2:0:5:w]\n"));
}
