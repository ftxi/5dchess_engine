#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <random>
#include <vector>
#include "hc_cell_tree.h"
#include "hypercuboid.h"

namespace
{
std::vector<point> grid()
{
    std::vector<point> result;
    for(int value = 0; value < 81; ++value)
    {
        point p(4);
        int remaining = value;
        for(auto &coordinate : p) { coordinate = remaining % 3; remaining /= 3; }
        result.push_back(std::move(p));
    }
    return result;
}

void randomized_ownership()
{
    const auto points = grid();
    std::mt19937 random(1771);
    for(auto policy : {hc_failure_policy::none, hc_failure_policy::all,
                       hc_failure_policy::expensive, hc_failure_policy::adaptive})
        for(std::size_t capacity : {1, 7, 64})
            for(int sample = 0; sample < 80; ++sample)
            {
                HC universe{{0,1,2}, {0,1,2}, {0,1,2}, {0,1,2}};
                hc_cell_tree tree(search_space{universe}, {policy, capacity, 4});
                auto resumed = tree.restrict(universe);
                std::vector<slice> negative;
                std::size_t work = 0;
                for(int step = 0; step < 16; ++step)
                {
                    slice problem;
                    for(index_t n = 0; n < 4; ++n)
                        if(random() % 2)
                        {
                            integer_set values;
                            for(index_t i = 0; i < 3; ++i) if(random() % 2) values.insert(i);
                            problem.fix_axis(n, std::move(values));
                        }
                    negative.push_back(problem);
                    tree.add_negative(std::move(problem));
                    tree.balance(universe);
                    for(int repeat = 0; repeat < 4; ++repeat)
                    {
                        HC restriction = universe;
                        for(index_t n = 0; n < 4; ++n)
                            if(random() % 2) restriction[n] = integer_set{index_t(random() % 3)};
                        const bool expected = std::any_of(points.begin(), points.end(), [&](const point &p) {
                            return restriction.contains(p) && std::none_of(negative.begin(), negative.end(),
                                [&](const slice &s) { return s.contains(p); });
                        });
                        const auto visitor = [&](const HC &domain, auto pending) {
                            work += 4;
                            bool exists = false;
                            for(const auto &p : points)
                            {
                                if(!domain.contains(p) || !restriction.contains(p)) continue;
                                if(std::any_of(pending.begin(), pending.end(),
                                    [&](const slice *s) { return s->contains(p); })) continue;
                                assert(std::none_of(negative.begin(), negative.end(),
                                    [&](const slice &s) { return s.contains(p); }));
                                exists = true;
                            }
                            return exists;
                        };
                        const bool actual = tree.visit(restriction, visitor, work);
                        assert(actual == expected);
                        assert(tree.visit(resumed, restriction, visitor, work) == expected);
                        const auto axis = index_t(random() % 4);
                        const auto supported = tree.supported(resumed, restriction, axis);
                        integer_set expected_values;
                        for(const auto &p : points)
                            if(restriction.contains(p) && std::none_of(negative.begin(), negative.end(),
                                [&](const slice &s) { return s.contains(p); }))
                            { assert(supported.contains(p[axis])); expected_values.insert(p[axis]); }
                        const auto projected = tree.project(resumed, restriction, axis, [&](const HC &domain, auto pending) {
                            integer_set values;
                            for(const auto &p : points)
                                if(domain.contains(p) && restriction.contains(p)
                                   && std::none_of(pending.begin(), pending.end(),
                                       [&](const slice *s) { return s->contains(p); })) values.insert(p[axis]);
                            return values;
                        }, true);
                        assert(std::ranges::equal(projected, expected_values));
                        const auto stats = tree.inspect();
                        assert(stats.cached_failures <= capacity);
                        assert(stats.cells <= stats.leaves * 2);
                    }
                }
                tree.remove_slice_globally(slice{});
                const auto stats = tree.inspect();
                assert(tree.empty() && stats.cells == 0 && stats.references == 0);
                assert(stats.problems == 0 && stats.cached_failures == 0);
            }
}

void resumed_frontiers()
{
    HC universe{{0,1,2}, {0,1,2}};
    hc_cell_tree tree(search_space{universe});
    auto original = tree.restrict(universe);
    tree.remove_slice_globally(slice{{{0, integer_set{2}}, {1, integer_set{2}}}});
    HC left{{0}, {0,1,2}};
    auto child = tree.restrict(left, &original);
    tree.add_negative(slice{{{1, integer_set{1}}}});
    std::size_t work = 0;
    const auto solve = [&](auto &frontier, const HC &restriction) {
        return tree.visit(frontier, restriction, [&](const HC &domain, auto pending) {
            ++work;
            for(auto x : domain[0]) for(auto y : domain[1])
            {
                point candidate{x,y};
                if(restriction.contains(candidate) && std::none_of(pending.begin(), pending.end(),
                    [&](const slice *problem) { return problem->contains(candidate); })) return true;
            }
            return false;
        }, work);
    };
    assert(!solve(child, HC{{0}, {1}}));
    assert(solve(child, left));
    tree.remove_slice_globally(slice{{{0, integer_set{1,2}}}});
    assert(tree.inspect().cells == 1);
    assert(solve(original, universe));
    assert(solve(child, left));
    tree.remove_slice_globally(slice{});
    assert(!solve(original, universe));
    assert(!solve(child, left));
    assert(tree.inspect().references == 0 && tree.inspect().cached_failures == 0);
}

void conditional_supports()
{
    HC universe{{0,1,2}, {0,1,2}};
    hc_cell_tree tree(search_space{universe});
    tree.add_negative(slice{{{0, integer_set{2}}, {1, integer_set{1}}}});
    auto candidates = tree.restrict(universe);
    HC restriction{{0,1,2}, {1}};
    auto supported = tree.supported(candidates, restriction, 0);
    assert(supported.contains(0) && supported.contains(1) && !supported.contains(2));
    assert(tree.supported(candidates, universe, 0).contains(2));
}

void proof_scope_and_reclamation()
{
    HC universe{{0,1,2}, {0,1,2}};
    hc_cell_tree tree(search_space{universe}, {hc_failure_policy::all, 1, 0});
    const slice removed{{{0, integer_set{2}}, {1, integer_set{2}}}};
    tree.remove_slice_globally(removed);
    std::size_t work = 0;
    auto solve = [&](const HC &restriction) {
        return tree.visit(restriction, [&](const HC &domain, auto) {
            ++work;
            for(auto x : domain[0]) for(auto y : domain[1])
                if(x != y && restriction.contains(point{x,y})) return true;
            return false;
        }, work);
    };
    HC failed{{0}, {0}};
    assert(!solve(failed));
    const auto previous = work;
    assert(!solve(failed) && work == previous);
    auto candidates = tree.restrict(universe);
    assert(!tree.supported(candidates, HC{{0,1,2}, {0}}, 0).contains(0));
    assert(tree.supported(candidates, HC{{0,1,2}, {0,1}}, 0).contains(0));
    assert(solve(universe));
    tree.remove_slice_globally(slice{{{0, integer_set{1,2}}}});
    assert(!solve(failed));
    assert(solve(universe));
    assert(tree.inspect().cells == 1);

    search_space singletons;
    for(index_t x = 0; x < 20; ++x) for(index_t y = 0; y < 20; ++y)
        singletons.push_back(HC{{x}, {y}});
    hc_cell_tree many(std::move(singletons), {hc_failure_policy::all, 7, 0});
    integer_set values;
    for(index_t x = 0; x < 20; ++x) values.insert(x);
    HC all{values, values};
    for(int pass = 0; pass < 3; ++pass)
        assert(!many.visit(all, [](const HC &, auto) { return false; }, work));
    many.remove_slice_globally(slice{});
    assert(many.inspect().cached_failures == 0 && many.inspect().cells == 0);
}

void complete_action_oracle()
{
    const auto position = state::from_pgn(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/1r2/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n");
    auto [info, initial] = HC_info::build_HC(position);
    std::vector<point> legal;
    auto pending = initial;
    while(!pending.empty())
    {
        HC region = std::move(pending.back());
        pending.pop_back();
        auto candidate = info.take_point(region);
        if(!candidate) continue;
        if(!info.find_problem(*candidate, info.get_universe())) legal.push_back(*candidate);
        pending.concat(std::move(region).remove_point(*candidate));
    }
    std::size_t arrival_replacements = 0;
    for(const auto &seed : legal)
    {
        auto fixed = info.get_universe();
        for(index_t n = 0; n < info.first_branch_axis(); ++n) fixed[n] = integer_set{seed[n]};
        assert(info.constrain_domain(fixed));
        for(index_t n = info.first_branch_axis(); n < info.dimension(); ++n)
        {
            assert(fixed[n].contains(seed[n]));
            if(seed[n] == 0) assert(fixed[n].size() == 1);
            else assert(!fixed[n].contains(0));
        }
        for(index_t n = 0; n < info.dimension(); ++n)
            for(auto coordinate : info.get_universe()[n])
                if(auto candidate = info.try_local_completion(seed, n, coordinate))
                {
                    assert(std::find(legal.begin(), legal.end(), *candidate) != legal.end());
                    assert((*candidate)[n] == coordinate);
                    for(index_t prefix = 0; prefix < n; ++prefix)
                        assert((*candidate)[prefix] == seed[prefix]);
                    if(coordinate != seed[n] && info.arrival_partner(n, coordinate))
                        ++arrival_replacements;
                }
    }
    assert(arrival_replacements > 0);
    {
        HC_info::completion_space space(initial);
        space.local_capacity = 1;
        std::mt19937 order_rng(291);
        random_HC_ordering shuffled(info.get_universe(), order_rng);
        const auto count = std::min<std::size_t>(legal.size(), 32);
        std::vector<std::vector<std::unique_ptr<HC_info::local_cursor>>> cursors(count);
        for(auto &prefixes : cursors) prefixes.resize(info.dimension());
        // Keep projected prefixes alive while unrelated legal classes replace
        // the small shared cache, then revisit them in the opposite order.
        for(int pass = 0; pass < 2; ++pass)
            for(std::size_t k = 0; k < count; ++k)
            {
                const auto j = pass ? count - k - 1 : k;
                const auto &seed = legal[j];
                for(index_t n = 0; n < info.dimension(); ++n)
                    for(auto coordinate : info.get_universe()[n])
                    {
                        const auto *parent = n ? cursors[j][n - 1].get() : nullptr;
                        auto candidate = pass
                            ? info.resume_local(space, cursors[j][n], parent, seed, n, coordinate, shuffled)
                            : info.resume_local(space, cursors[j][n], parent, seed, n, coordinate, natural_HC_ordering{});
                        if(!candidate) continue; // Class failure does not reject the full prefix.
                        assert((*candidate)[n] == coordinate);
                        for(index_t prefix = 0; prefix < n; ++prefix) assert((*candidate)[prefix] == seed[prefix]);
                        assert(std::find(legal.begin(), legal.end(), *candidate) != legal.end());
                    }
            }
        assert(space.local_resume_hits > 0);
    }
    // Mixed restrictions exercise simultaneous forced departures, competing
    // arrival axes and later fixed-point rounds, not only one-axis probes.
    std::mt19937 domain_rng(1729);
    HC_info::constraint_workspace scratch;
    for(int iteration = 0; iteration < 256; ++iteration)
    {
        HC restriction = info.get_universe();
        for(index_t n = 0; n < info.dimension(); ++n)
        {
            integer_set kept;
            for(auto coordinate : restriction[n])
                if(domain_rng() % 4 != 0) kept.insert(coordinate);
            restriction[n] = std::move(kept);
        }
        HC constrained = restriction;
        const bool consistent = info.constrain_domain(constrained);
        HC reused = restriction;
        const bool reused_consistent = info.constrain_domain(reused, &scratch);
        assert(reused_consistent == consistent);
        if(consistent)
            for(index_t n = 0; n < info.dimension(); ++n)
                assert(reused[n].is_subset_of(constrained[n]) && constrained[n].is_subset_of(reused[n]));
        for(const auto &p : legal)
            if(restriction.contains(p)) assert(consistent && constrained.contains(p));
    }
    {
        const auto other_position = state::from_pgn(
            "[Board \"custom\"]\n[Size \"4x4\"]\n"
            "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n[3k/4/4/KN2:2:1:w]\n");
        auto [other, other_initial] = HC_info::build_HC(other_position);
        // The reusable workspace may move between contexts and dimensions;
        // cached incidence and conservation totals must be replaced together.
        for(int iteration = 0; iteration < 64; ++iteration)
        {
            const auto &current = iteration % 2 ? info : other;
            HC domain = current.get_universe();
            for(index_t n = 0; n < current.dimension(); ++n)
                if(domain_rng() % 2)
                {
                    integer_set kept;
                    for(auto coordinate : domain[n]) if(domain_rng() % 2) kept.insert(coordinate);
                    domain[n] = std::move(kept);
                }
            HC reference = domain;
            const bool expected = current.constrain_domain(reference);
            assert(current.constrain_domain(domain, &scratch) == expected);
            if(expected)
                for(index_t n = 0; n < current.dimension(); ++n)
                    assert(domain[n].is_subset_of(reference[n]) && reference[n].is_subset_of(domain[n]));
        }
    }
    // Reuse a prepared prefix query across singleton restrictions, including
    // arrival coordinates whose paired departure has been excluded.
    {
        HC_info::completion_space space(initial);
        for(index_t n = 0; n < info.dimension(); ++n)
        {
            auto query = info.restrict_completion(space, info.get_universe(), n);
            for(auto coordinate : info.get_universe()[n])
            {
                HC restriction = info.get_universe();
                restriction[n] = integer_set{coordinate};
                for(int pass = 0; pass < 2; ++pass)
                {
                    if(pass == 1)
                        if(auto partner = info.arrival_partner(n, coordinate))
                            restriction[partner->first].erase(partner->second);
                    const bool exists = std::any_of(legal.begin(), legal.end(),
                        [&](const point &p) { return restriction.contains(p); });
                    auto candidate = info.find_witness(space, restriction, natural_HC_ordering{}, &query);
                    assert(bool(candidate) == exists);
                    if(candidate) assert(restriction.contains(*candidate));
                }
            }
        }
    }
    for(auto policy : {hc_failure_policy::none, hc_failure_policy::all,
                       hc_failure_policy::expensive, hc_failure_policy::adaptive})
        for(bool local : {false, true})
        {
            HC_info::completion_space space(initial, {policy, 7, 4});
            space.reuse_local = local;
            space.local_capacity = 2;
            std::vector<HC> restrictions{info.get_universe()};
            for(index_t n = 0; n < info.dimension(); ++n)
                for(auto coordinate : info.get_universe()[n])
                {
                    HC restriction = info.get_universe();
                    restriction[n] = integer_set{coordinate};
                    restrictions.push_back(std::move(restriction));
                }
            std::mt19937 random(42);
            std::shuffle(restrictions.begin(), restrictions.end(), random);
            for(int pass = 0; pass < 2; ++pass)
                for(const auto &restriction : restrictions)
                {
                    HC constrained = restriction;
                    const bool consistent = info.constrain_domain(constrained);
                    const auto candidate = info.find_witness(space, restriction);
                    const bool exists = std::any_of(legal.begin(), legal.end(),
                        [&](const point &p) { return restriction.contains(p); });
                    assert(bool(candidate) == exists);
                    for(const auto &p : legal)
                        if(restriction.contains(p)) assert(consistent && constrained.contains(p));
                    if(candidate)
                    {
                        assert(restriction.contains(*candidate));
                        assert(std::find(legal.begin(), legal.end(), *candidate) != legal.end());
                    }
                }
        }
}
} /* anonymous namespace */

int main()
{
    randomized_ownership();
    resumed_frontiers();
    conditional_supports();
    proof_scope_and_reclamation();
    complete_action_oracle();
}
