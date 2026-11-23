#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <random>
#include <string>
#include <vector>

#include "hypercuboid.h"

namespace
{
bool invalid_jump_pattern(const HC_info &info, const point &p)
{
    const auto first = info.first_branch_axis();
    bool passed = false;
    std::vector<full_move> branches;
    for(index_t n = first; n < info.dimension(); ++n)
    {
        if(p[n] == 0) { passed = true; continue; }
        if(passed) return true;
        const auto semi = info.get_semimove(n, p[n]);
        branches.push_back(semi.get<arriving_move>().m);
    }
    const auto &position = info.get_state();
    for(const auto &move : branches)
        if(auto target = info.line_to_axis(move.to.l()); target && p[*target] == 0
           && position.get_timeline_end(move.to.l()) == turn{move.to.t(), position.get_present().c()})
            return true;
    for(std::size_t earlier = 0; earlier < branches.size(); ++earlier)
        for(std::size_t later = earlier + 1; later < branches.size(); ++later)
            if(branches[earlier].to.tl() == branches[later].from.tl()) return true;
    return false;
}

index_t departure_coordinate(const HC_info &info, const full_move &move)
{
    const auto axis = *info.line_to_axis(move.from.l());
    for(index_t i : info.get_universe()[axis])
    {
        const auto semi = info.get_semimove(axis, i);
        if(const auto *departure = semi.get_if<departing_move>(); departure && departure->from == move.from)
            return i;
    }
    assert(false && "arrival must have a departure");
    return 0;
}

index_t arrival_coordinate(const HC_info &info, index_t axis, const full_move &move)
{
    for(index_t i : info.get_universe()[axis])
    {
        const auto semi = info.get_semimove(axis, i);
        if(const auto *arrival = semi.get_if<arriving_move>(); arrival && arrival->m == move) return i;
    }
    assert(false && "missing branch arrival");
    return 0;
}

bool excluded(const std::vector<slice> &problems, const point &p)
{
    return std::any_of(problems.begin(), problems.end(), [&](const slice &problem) { return problem.contains(p); });
}

void check_soundness(const HC_info &info, const HC &domain, const std::vector<slice> &problems)
{
    std::mt19937 random(20261112);
    std::vector<std::vector<index_t>> coordinates(info.dimension());
    for(index_t n = 0; n < info.dimension(); ++n)
        coordinates[n].assign(domain[n].begin(), domain[n].end());
    for(const auto &problem : problems)
    {
        for(const auto &[axis, values] : problem.get_fixed_axes())
        {
            assert(!values.empty());
            assert(values.is_subset_of(domain[axis]));
        }
        for(int sample = 0; sample < 64; ++sample)
        {
            point p(info.dimension());
            for(index_t n = 0; n < info.dimension(); ++n)
                p[n] = coordinates[n][random() % coordinates[n].size()];
            for(const auto &[axis, values] : problem.get_fixed_axes())
            {
                std::vector<index_t> fixed(values.begin(), values.end());
                p[axis] = fixed[random() % fixed.size()];
            }
            assert(problem.contains(p));
            assert(invalid_jump_pattern(info, p));
        }
    }
}

void test_related_problems(bool player)
{
    const std::string board = player ? "3K/4/4/kn2" : "3k/4/4/KN2";
    std::string pgn = "[Board \"custom\"]\n[Size \"4x4\"]\n";
    for(int line = 0; line < 3; ++line)
        pgn += "[" + board + ":" + std::to_string(line) + ":1:" + (player ? "b" : "w") + "]\n";
    const auto position = state::from_pgn(pgn);
    auto [info, initial] = HC_info::build_HC(position);
    const auto first = info.first_branch_axis();
    const auto branches = info.dimension() - first;
    assert(branches >= 3);
    std::vector<full_move> arrivals;
    for(index_t i : info.get_universe()[first])
    {
        const auto semi = info.get_semimove(first, i);
        if(const auto *arrival = semi.get_if<arriving_move>()) arrivals.push_back(arrival->m);
    }
    std::optional<std::pair<full_move, full_move>> reciprocal;
    for(const auto &a : arrivals)
        for(const auto &b : arrivals)
            if(a.from.tl() == b.to.tl() && a.to.tl() == b.from.tl()) reciprocal = std::pair{a, b};
    assert(reciprocal);
    const auto [into_board, from_board] = *reciprocal;

    point passed(info.dimension(), 0);
    passed[*info.line_to_axis(into_board.from.l())] = departure_coordinate(info, into_board);
    passed[first] = arrival_coordinate(info, first, into_board);
    std::vector<slice> related;
    auto primary = info.jump_order_consistent(passed, info.get_universe(), &related);
    assert(primary && primary->contains(passed));
    assert(related.size() == std::size_t(branches - 1));
    assert(primary->to_string() == info.jump_order_consistent(passed, info.get_universe())->to_string());
    auto problems = related;
    problems.push_back(*primary);
    check_soundness(info, info.get_universe(), problems);
    for(index_t branch = first; branch < info.dimension(); ++branch)
    {
        auto shifted = passed;
        shifted[first] = 0;
        shifted[branch] = arrival_coordinate(info, branch, into_board);
        assert(excluded(problems, shifted));
    }

    point reversed(info.dimension(), 0);
    reversed[*info.line_to_axis(into_board.from.l())] = departure_coordinate(info, into_board);
    reversed[*info.line_to_axis(from_board.from.l())] = departure_coordinate(info, from_board);
    reversed[first] = arrival_coordinate(info, first, into_board);
    reversed[first + 1] = arrival_coordinate(info, first + 1, from_board);
    primary = info.jump_order_consistent(reversed, info.get_universe(), &related);
    assert(primary && primary->contains(reversed));
    assert(related.size() + 1 == std::size_t(branches) * (branches - 1) / 2);
    problems = related;
    problems.push_back(*primary);
    check_soundness(info, info.get_universe(), problems);
    for(index_t earlier = first; earlier < info.dimension(); ++earlier)
        for(index_t later = earlier + 1; later < info.dimension(); ++later)
        {
            auto shifted = reversed;
            for(index_t branch = first; branch < info.dimension(); ++branch) shifted[branch] = 0;
            shifted[earlier] = arrival_coordinate(info, earlier, into_board);
            shifted[later] = arrival_coordinate(info, later, from_board);
            assert(excluded(problems, shifted));
        }

    // A local domain may make some related branch placements unavailable.
    HC restricted = info.get_universe();
    restricted[info.dimension() - 1] = integer_set{0};
    primary = info.jump_order_consistent(reversed, restricted, &related);
    assert(primary);
    problems = related;
    problems.push_back(*primary);
    check_soundness(info, restricted, problems);
    assert(problems.size() == std::size_t(branches - 1) * (branches - 2) / 2);

    point empty(info.dimension(), 0);
    assert(!info.jump_order_consistent(empty, info.get_universe(), &related));
    assert(related.empty());
    primary = info.find_problem(reversed, info.get_universe(), &related);
    assert(primary && !related.empty());
}
} /* anonymous namespace */

int main()
{
    test_related_problems(false);
    test_related_problems(true);
}
