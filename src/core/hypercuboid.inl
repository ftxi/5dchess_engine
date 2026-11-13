
#include <algorithm>
#include <cassert>
#include <limits>
#include <iterator>
#include <ranges>
#include "graph.h"

template<HCOrdering Ordering>
std::optional<point> HC_info::match_point(
    HC &hc, const Ordering &order, workspace &ws, bool reuse_matching) const
{
    assert(hc.dimension() == dimension());
    if(ws.count_matching) ++ws.matching_calls;
    auto &g = ws.matching_graph;
    g.reset(dimension());
    auto &must_include = ws.must_include;
    must_include.clear();
    constexpr index_t invalid_index = std::numeric_limits<index_t>::max();

    const size_t num_edges = static_cast<std::size_t>(dimension()) * dimension();
    auto &edge_refs = ws.edge_refs;
    edge_refs.assign(num_edges, invalid_index);
    const auto edge_ref = [&](index_t p, index_t q) -> index_t & {
        return edge_refs[static_cast<std::size_t>(p) * dimension() + q];
    };

    point result(dimension(), invalid_index);
    for(index_t n = 0; n < dimension(); n++)
    {
        bool has_nonjump = false;
        auto &ghost_arrive_indices = ws.ghost_arrive_indices;
        ghost_arrive_indices.clear();
        for(index_t i : order(n, hc[n]))
        {
            if(nonjump_entries[n].contains(i))
            {
                if(!has_nonjump)
                {
                    has_nonjump = true;
                    result[n] = i;
                }
            }
            else if(arriving_entries[n].contains(i))
            {
                const auto &arriving = std::get<arriving_entry>(axis_coords[n][i]);
                index_t from_axis = line_to_axis_unsafe(arriving.m.from.l());
                assert(from_axis != no_axis);
                if(!hc[from_axis].contains(arriving.idx))
                {
                    ghost_arrive_indices.insert(i);
                    continue;
                }
                index_t &departure_ref = edge_ref(from_axis, n);
                if(departure_ref == invalid_index)
                {
                    g.add_edge(from_axis, n);
                    assert(from_axis != n);
                    departure_ref = arriving.idx;
                    edge_ref(n, from_axis) = i;
                    assert(arriving.idx != invalid_index);
                }
            }
        }

        hc[n].minus(ghost_arrive_indices);
        if(hc[n].empty())
        {
            return std::nullopt;
        }
        if(!has_nonjump)
        {
            must_include.push_back(n);
        }
    }

    auto matching = g.find_matching(must_include, ws.matching_scratch, reuse_matching
        ? std::span<const std::pair<index_t, index_t>>(ws.matching_seed)
        : std::span<const std::pair<index_t, index_t>>{});
    if(!matching)
    {
        return std::nullopt;
    }
    for(const auto &[u, v] : *matching)
    {
        result[u] = edge_ref(u, v);
        result[v] = edge_ref(v, u);
        assert(result[u] != invalid_index);
        assert(result[v] != invalid_index);
    }
    // Keep the last nonempty successful matching, including across failures.
    // Every subsequent call validates its edges against the current graph.
    if(!must_include.empty()) ws.matching_seed = std::move(*matching);
#ifndef NDEBUG
    for(index_t i : result)
    {
        assert(i != invalid_index && "some axis is still null");
    }
#endif
    assert(hc.contains(result));
    return result;
}

template<HCOrdering Ordering>
std::optional<point> HC_info::take_point(
    HC &hc, const Ordering &order, workspace *ws, std::span<const slice> negative) const
{
    if(!ws)
    {
        workspace local;
        return take_point(hc, order, &local, negative);
    }
    if(negative.empty())
    {
        if(!ws->reuse_between_points) ws->matching_seed.clear();
        return match_point(hc, order, *ws, ws->reuse_between_points);
    }

    // Explore the complement lazily: a temporary region at depth k already
    // avoids slices [0, k). Matching is only run after all slices are avoided.
    std::vector<std::pair<HC, std::size_t>> pending;
    pending.emplace_back(hc, 0);
    while(!pending.empty())
    {
        auto [region, next] = std::move(pending.back());
        pending.pop_back();
        if(region.empty()) continue;
        while(next < negative.size() && !region.intersects(negative[next])) ++next;
        if(next == negative.size())
        {
            if(auto pt = match_point(region, order, *ws, true)) return pt;
            continue;
        }
        auto pieces = std::move(region).remove_slice_carefully(negative[next]);
        for(HC &piece : pieces)
            pending.emplace_back(std::move(piece), next + 1);
    }
    return std::nullopt;
}

template<HCOrdering Ordering, hc_policy Policy>
generator<moveseq> HC_info::local_search(
    const HC &outer_hc, local_space &local, const point &seed, const Ordering &order,
    bool seed_already_yielded,
    std::span<const slice> negative) const
{
    struct local_departure
    {
        index_t variable;
        index_t axis;
        const integer_set &values;
    };
    std::vector<local_departure> departures;
    for(index_t variable = 0; variable < local.axes.size(); ++variable)
    {
        const index_t arrival_axis = local.axes[variable];
        if(!arriving_entries[arrival_axis].contains(local.seed[variable])) continue;
        const auto &arrival = std::get<arriving_entry>(
            axis_coords[arrival_axis][local.seed[variable]]);
        const index_t departure_axis = line_to_axis_unsafe(arrival.m.from.l());
        const auto &fixed = local.outer_region.get_fixed_axes();
        const auto found = fixed.find(departure_axis);
        // An omitted outer restriction means the parent HC already has exactly
        // this departure domain. Both domains outlive the local enumeration.
        const auto &values = found == fixed.end() ? outer_hc[departure_axis] : found->second;
        if(values.size() > 1) departures.push_back({variable, departure_axis, values});
    }
    const auto project_problem = [&](const slice &problem) {
        return project_local_problem(local, seed, problem);
    };

    search_space remaining = seed_already_yielded
        ? local.universe.remove_point(local.seed) : search_space{local.universe};
    // Imported constraints keep their original guards outside this projection.
    for(const slice &problem : negative)
        if(auto projected = project_problem(problem))
            remaining.remove_slice_globally(*projected);

    const auto select = [&](const HC &hc) -> std::optional<point> {
        point selected(local.axes.size());
        for(index_t n = 0; n < local.axes.size(); ++n)
        {
            auto &&coordinates = order(local.axes[n], hc[n]);
            auto first = coordinates.begin();
            if(first == coordinates.end()) return std::nullopt;
            selected[n] = *first;
        }
        return selected;
    };
    std::vector<slice> retained;
    std::size_t num_problems = 0;
    point full = seed;
    while(!remaining.empty())
    {
        HC hc = std::move(remaining.back());
        remaining.pop_back();
        if constexpr(Policy == hc_policy::deferred || Policy == hc_policy::tree)
            if(!prune_retained_slices(hc, retained)) continue;
        auto selected = select(hc);
        if(!selected) continue;
        if(std::any_of(retained.begin(), retained.end(),
            [&](const slice &problem) { return problem.contains(*selected); }))
        {
            // All local coordinates already satisfy the structural checks;
            // temporary negative-slice traversal needs no matching at its leaves.
            std::vector<std::pair<HC, std::size_t>> pending;
            pending.emplace_back(hc, 0);
            selected.reset();
            while(!pending.empty())
            {
                auto [region, next] = std::move(pending.back());
                pending.pop_back();
                if(region.empty()) continue;
                while(next < retained.size() && !region.intersects(retained[next])) ++next;
                if(next == retained.size())
                {
                    selected = select(region);
                    if(selected) break;
                    continue;
                }
                auto pieces = std::move(region).remove_slice_carefully(retained[next]);
                for(HC &piece : pieces) pending.emplace_back(std::move(piece), next + 1);
            }
        }
        if(!selected) continue;
        for(index_t n = 0; n < local.axes.size(); ++n) full[local.axes[n]] = (*selected)[n];
        for(const auto &departure : departures)
            full[departure.axis] = std::get<arriving_entry>(
                axis_coords[local.axes[departure.variable]][(*selected)[departure.variable]]).idx;
        auto problem = find_checks(full, universe);
        if(!problem)
        {
            co_yield to_action(full);
            remaining.concat(std::move(hc).remove_point(*selected));
            continue;
        }

        // Save the unprojected problem: fixed jump/null guards must survive
        // when it is later propagated through the outer search space.
        local.problems.push_back(std::move(*problem));
        auto projected = project_problem(local.problems.back());
        assert(projected && projected->contains(*selected));
        ++num_problems;
        if constexpr(Policy == hc_policy::iterative)
        {
            for(const auto &[axis, values] : projected->get_fixed_axes())
                projected->fix_axis(axis, hc[axis] & values);
            remaining.concat(std::move(hc).remove_slice(*projected));
            continue;
        }
        remaining.push_back(std::move(hc));
        if constexpr(Policy == hc_policy::deferred || Policy == hc_policy::tree)
        {
            if(projected->get_fixed_axes().size() <= 1)
            {
                remaining.remove_slice_globally(*projected);
                continue;
            }
            retained.push_back(std::move(*projected));
            if(retained.size() > remaining.size())
            {
                const auto cheapest = std::min_element(retained.begin(), retained.end(),
                    [](const slice &a, const slice &b) {
                        return a.get_fixed_axes().size() < b.get_fixed_axes().size();
                    });
                remaining.remove_slice_globally(*cheapest);
                retained.erase(cheapest);
            }
        }
        else if constexpr(Policy == hc_policy::balanced)
        {
            if(num_problems <= 100) remaining.remove_slice_backwards(*projected, true, 10);
            else remaining.remove_slice_globally(*projected);
        }
        else
        {
            remaining.remove_slice_globally(*projected);
        }
    }
}

template<HCOrdering Ordering, hc_policy Policy>
generator<moveseq> HC_info::search(search_space ss, Ordering order) const &
{
    // Mandatory timelines occupy the first axes. Physical moves on all of
    // them, with null entries everywhere else, need only the check constraint.
    if(!mandatory_lines.empty() && !ss.empty())
    {
        slice physical_region;
        bool available = true;
        for(index_t n = 0; n < dimension(); ++n)
        {
            integer_set values{null_coordinate};
            if(n < mandatory_lines.size())
            {
                values = nonjump_entries[n];
                values.erase(null_coordinate);
            }
            else values &= universe[n];
            if(values.empty())
            {
                available = false;
                break;
            }
            if(!universe[n].is_subset_of(values)) physical_region.fix_axis(n, std::move(values));
        }
        if(available)
        {
            search_space physical_space;
            for(const HC &hc : ss)
            {
                if(hc.empty() || !hc.intersects(physical_region)) continue;
                HC region = hc;
                for(const auto &[n, values] : physical_region.get_fixed_axes()) region[n] &= values;
                physical_space.push_back(std::move(region));
            }
            while(!physical_space.empty())
            {
                HC hc = std::move(physical_space.back());
                physical_space.pop_back();
                local_space local;
                point seed(dimension(), null_coordinate);
                std::vector<integer_set> coordinates;
                for(index_t n = 0; n < mandatory_lines.size(); ++n)
                {
                    auto &&values = order(n, hc[n]);
                    seed[n] = *values.begin();
                    if(hc[n].size() <= 1) continue;
                    local.axes.push_back(n);
                    local.seed.push_back(seed[n]);
                    coordinates.push_back(hc[n]);
                }
                local.universe = HC(std::move(coordinates));
                auto actions = local_search<Ordering, Policy>(hc, local, seed, order, false);
                while(auto moves = actions.first()) co_yield std::move(*moves);
                for(const slice &problem : local.problems)
                {
                    ss.remove_slice_globally(problem);
                    physical_space.remove_slice_globally(problem);
                }
            }
            // Defer splitting the outer space until the local phase drains;
            // first-action callers need not pay for removing this region.
            ss.remove_slice_globally(physical_region);
        }
    }
    auto active = [&] {
        if constexpr(Policy == hc_policy::tree) return hc_tree(std::move(ss));
        else return std::move(ss);
    }();
    workspace ws;
    ws.reuse_between_points = (Policy == hc_policy::deferred || Policy == hc_policy::tree);
    std::vector<slice> negative;
    std::vector<slice> additional_problems;
    std::size_t num_problems = 0;
    constexpr std::size_t adaptive_problem_limit = 100;
    const auto codimension = [&](const slice &problem) {
        index_t result = 0;
        for(const auto &[axis, values] : problem.get_fixed_axes())
            result += !universe[axis].is_subset_of(values);
        return result;
    };
    while(!active.empty())
    {
        if constexpr(Policy == hc_policy::balanced)
            if(num_problems == adaptive_problem_limit) break;
        HC hc = [&] {
            if constexpr(Policy == hc_policy::tree) return active.take_back();
            else {
                HC result = std::move(active.back());
                active.pop_back();
                return result;
            }
        }();
        if constexpr(Policy == hc_policy::deferred || Policy == hc_policy::tree)
            if(!prune_retained_slices(hc, negative)) continue;
        auto pt = take_point(hc, order, &ws);
        if constexpr(Policy == hc_policy::deferred || Policy == hc_policy::tree)
        {
            if(!pt || !prune_retained_slices(hc, negative)) continue;
            if(std::any_of(negative.begin(), negative.end(),
                [&](const slice &problem) { return problem.contains(*pt); }))
                pt = take_point(hc, order, &ws, negative);
        }
        if(!pt) continue;
        auto problem = [&] {
            if constexpr(Policy == hc_policy::stable || Policy == hc_policy::deferred || Policy == hc_policy::tree)
                return find_problem(*pt, universe, &additional_problems);
            else return find_problem(*pt, hc, &additional_problems);
        }();
        if(!problem)
        {
            co_yield to_action(*pt);
            if(auto local = build_local_space(hc, *pt))
            {
                auto actions = local_search<Ordering, Policy>(hc, *local, *pt, order, true, negative);
                while(auto moves = actions.first()) co_yield std::move(*moves);
                active.concat(std::move(hc).remove_slice(local->outer_region));
                for(const slice &problem : local->problems) active.remove_slice_globally(problem);
            }
            else active.concat(std::move(hc).remove_point(*pt));
            continue;
        }
        if constexpr(Policy == hc_policy::iterative)
        {
            auto pieces = std::move(hc).remove_slice(*problem);
            for(const auto &related : additional_problems) pieces.remove_slice_globally(related);
            active.concat(std::move(pieces));
        }
        else
        {
            active.push_back(std::move(hc));
            if constexpr(Policy == hc_policy::balanced) ++num_problems;
            const auto remove_problem = [&](slice problem, bool primary) {
                if constexpr(Policy == hc_policy::balanced)
                    active.remove_slice_backwards(problem, primary, 10);
                else if constexpr(Policy == hc_policy::stable)
                    active.remove_slice_globally(problem);
                else
                {
                    if(codimension(problem) <= 1) active.remove_slice_globally(problem);
                    else
                    {
                        negative.push_back(std::move(problem));
                        if(negative.size() > active.size())
                        {
                            const auto cheapest = std::min_element(negative.begin(), negative.end(),
                                [&](const slice &a, const slice &b) { return codimension(a) < codimension(b); });
                            active.remove_slice_globally(*cheapest);
                            negative.erase(cheapest);
                        }
                    }
                }
            };
            remove_problem(std::move(*problem), true);
            for(auto &related : additional_problems)
            {
                if(active.empty()) break;
                remove_problem(std::move(related), false);
            }
        }
    }
    if constexpr(Policy == hc_policy::balanced)
        for(auto &&moves : search<Ordering, hc_policy::stable>(std::move(active), std::move(order)))
            co_yield std::move(moves);
}

// This query path deliberately has no physical-first/local draining phase:
// both representations answer the same fixed restrictions without consuming
// witnesses. No mutation of positive space occurs during its traversal.
template<hc_policy Policy, HCOrdering Ordering>
std::optional<point> HC_info::find_witness(witness_space<Policy> &space,
                                        const HC &restriction,
                                        const Ordering &order) const
{
    assert(restriction.dimension() == dimension());
    slice query;
    for(index_t n = 0; n < dimension(); ++n)
        query.fix_axis(n, restriction[n]);
    space.scratch.reuse_between_points = true;
    const auto codimension = [&](const slice &problem) {
        index_t result = 0;
        for(const auto &[axis, values] : problem.get_fixed_axes())
            result += !universe[axis].is_subset_of(values);
        return result;
    };
    std::vector<slice> additional_problems;
    for(;;)
    {
        additional_problems.clear();
        std::optional<point> witness;
        std::optional<slice> problem;
        const auto visit = [&](const HC &bounds) {
            HC region = bounds;
            for(index_t n = 0; n < dimension(); ++n) region[n] &= restriction[n];
            if(region.empty() || !prune_retained_slices(region, space.negative)) return false;
            auto candidate = take_point(region, order, &space.scratch);
            if(!candidate || !prune_retained_slices(region, space.negative)) return false;
            if(std::any_of(space.negative.begin(), space.negative.end(),
                [&](const slice &negative) { return negative.contains(*candidate); }))
                candidate = take_point(region, order, &space.scratch, space.negative);
            if(!candidate) return false;
            problem = find_problem(*candidate, universe, &additional_problems);
            if(!problem) witness = std::move(candidate);
            return true;
        };
        if constexpr(Policy == hc_policy::tree)
            space.positive.visit_intersections(query, visit);
        else
        {
            // Match the tree's reverse DFS leaf order.
            for(const HC &hc : std::views::reverse(space.positive))
                if(hc.intersects(query) && visit(hc)) break;
        }
        if(witness || !problem) return witness;
        const auto publish = [&](slice exclusion) {
            if(codimension(exclusion) <= 1)
                space.positive.remove_slice_globally(exclusion);
            else
            {
                space.negative.push_back(std::move(exclusion));
                if(space.negative.size() > space.positive.size())
                {
                    const auto cheapest = std::min_element(space.negative.begin(), space.negative.end(),
                        [&](const slice &a, const slice &b) { return codimension(a) < codimension(b); });
                    space.positive.remove_slice_globally(*cheapest);
                    space.negative.erase(cheapest);
                }
            }
        };
        publish(std::move(*problem));
        for(auto &related : additional_problems) publish(std::move(related));
    }
}


template<HCOrdering Ordering>
std::optional<point> HC_info::take_local_point(
    const local_space &local, const point &seed, const HC &region, const Ordering &order) const
{
    if(!region.intersects(local.outer_region)) return std::nullopt;
    point candidate = seed;
    for(std::size_t variable = 0; variable < local.axes.size(); ++variable)
    {
        const auto axis = local.axes[variable];
        const auto allowed = region[axis] & local.universe[variable];
        bool found = false;
        for(auto coordinate : order(axis, allowed))
        {
            if(const auto *arrival = std::get_if<arriving_entry>(&axis_coords[axis][coordinate]))
            {
                const auto source = line_to_axis_unsafe(arrival->m.from.l());
                if(!region[source].contains(arrival->idx)) continue;
                candidate[source] = arrival->idx;
            }
            candidate[axis] = coordinate;
            found = true;
            break;
        }
        if(!found) return std::nullopt;
    }
    if(!region.contains(candidate)) return std::nullopt;
    return candidate;
}

template<HCOrdering Ordering>
std::optional<point> HC_info::local_witness(
    local_space &local, const point &seed, const HC &restriction, const Ordering &order) const
{
    if(!restriction.intersects(local.outer_region)) return std::nullopt;
    std::optional<slice> first_problem;
    if(!local.candidates)
    {
        auto candidate = take_local_point(local, seed, restriction, order);
        if(!candidate) return std::nullopt;
        first_problem = find_checks(*candidate, universe);
        if(!first_problem) return candidate;
        local.candidates = std::make_unique<hc_cell_tree>(
            search_space{local.universe}, hc_cell_options{hc_failure_policy::all, 64, 0});
    }
    if(local.candidates->empty()) return std::nullopt;
    HC projected = local.universe;
    for(index_t n = 0; n < local.axes.size(); ++n)
    {
        const auto axis = local.axes[n];
        projected[n] &= restriction[axis];
        if(arriving_entries[axis].contains(local.seed[n]))
        {
            integer_set unsupported;
            for(auto coordinate : projected[n])
            {
                const auto &arrival = std::get<arriving_entry>(axis_coords[axis][coordinate]);
                if(!restriction[line_to_axis_unsafe(arrival.m.from.l())].contains(arrival.idx))
                    unsupported.insert(coordinate);
            }
            projected[n].minus(unsupported);
        }
    }
    if(projected.empty()) return std::nullopt;
    const auto select = [&](const HC &domain) {
        point full = seed;
        for(index_t n = 0; n < local.axes.size(); ++n)
        {
            const auto axis = local.axes[n];
            auto values = order(axis, domain[n]);
            full[axis] = *values.begin();
            if(const auto *arrival = std::get_if<arriving_entry>(&axis_coords[axis][full[axis]]))
                full[line_to_axis_unsafe(arrival->m.from.l())] = arrival->idx;
        }
        return full;
    };
    // Fixed guards outside the local variables must hold before a failure can
    // be memoized solely in projected coordinates.
    if(!restriction.contains(select(projected))) return std::nullopt;
    const auto publish = [&](slice problem) {
        auto cut = project_local_problem(local, seed, problem);
        assert(cut);
        local.problems.push_back(std::move(problem));
        if(cut->get_fixed_axes().size() <= 1) local.candidates->remove_slice_globally(*cut);
        else
        {
            local.candidates->add_negative(std::move(*cut));
            local.candidates->balance(local.universe);
        }
    };
    if(first_problem) publish(std::move(*first_problem));
    const std::size_t work = 0;
    for(;;)
    {
        std::optional<point> result;
        std::optional<slice> problem;
        local.candidates->visit(projected, [&](const HC &bounds, auto negative) {
            HC domain = bounds;
            for(index_t n = 0; n < local.axes.size(); ++n) domain[n] &= projected[n];
            if(!prune_slice_refs(domain, negative)) return false;
            std::vector<std::pair<HC, std::size_t>> pending;
            pending.emplace_back(std::move(domain), 0);
            while(!pending.empty())
            {
                auto [part, next] = std::move(pending.back());
                pending.pop_back();
                while(next < negative.size() && !part.intersects(*negative[next])) ++next;
                if(next != negative.size())
                {
                    auto pieces = std::move(part).remove_slice_carefully(*negative[next]);
                    for(auto &piece : pieces) pending.emplace_back(std::move(piece), next + 1);
                    continue;
                }
                auto candidate = select(part);
                problem = find_checks(candidate, universe);
                if(!problem) result = std::move(candidate);
                return true;
            }
            return false;
        }, work);
        if(result || !problem) return result;
        publish(std::move(*problem));
    }
}


template<HCOrdering Ordering>
std::optional<point> HC_info::resume_local(completion_space &space,
    std::unique_ptr<local_cursor> &cursor, const local_cursor *parent,
    const point &seed, index_t axis, index_t coordinate, const Ordering &order) const
{
    if(!prepare_local_cursor(space, cursor, parent, seed, axis, coordinate)) return std::nullopt;
    ++space.local_resumes;
    auto &local = *cursor->local;
    const auto &layout = *local.projection;
    HC projected = cursor->domain;
    if(!restrict_local_axis(projected, local, axis, coordinate)) return std::nullopt;
    const auto select = [&](const HC &domain) {
        point full = local.representative;
        for(index_t variable = 0; variable < local.axes.size(); ++variable)
        {
            const auto n = local.axes[variable];
            auto values = order(n, domain[variable]);
            full[n] = *values.begin();
            if(layout.sources[variable] != no_axis)
                full[layout.sources[variable]] = std::get<arriving_entry>(axis_coords[n][full[n]]).idx;
        }
        assert(full[axis] == coordinate);
        for(index_t n = 0; n < axis; ++n) assert(full[n] == seed[n]);
        return full;
    };
    const auto publish = [&](const slice &problem) {
        auto cut = project_local_problem(local, local.representative, problem);
        assert(cut);
        if(cut->get_fixed_axes().size() <= 1) local.candidates->remove_slice_globally(*cut);
        else
        {
            local.candidates->add_negative(std::move(*cut));
            local.candidates->balance(local.universe);
        }
    };
    if(!local.candidates)
    {
        auto candidate = select(projected);
        auto problem = find_checks(candidate, universe);
        if(!problem) { ++space.local_resume_hits; return candidate; }
        local.candidates = std::make_unique<hc_cell_tree>(
            search_space{local.universe}, hc_cell_options{hc_failure_policy::all, 64, 0});
        publish(*problem);
    }
    if(!cursor->frontier_ready)
    {
        cursor->candidates = local.candidates->restrict(cursor->domain);
        cursor->frontier_ready = true;
    }
    const std::size_t work = 0;
    for(;;)
    {
        std::optional<point> result;
        std::optional<slice> problem;
        local.candidates->visit(cursor->candidates, projected, [&](const HC &bounds, auto negative) {
            HC domain = bounds;
            for(index_t n = 0; n < local.axes.size(); ++n) domain[n] &= projected[n];
            if(!prune_slice_refs(domain, negative)) return false;
            std::vector<std::pair<HC, std::size_t>> pending;
            pending.emplace_back(std::move(domain), 0);
            while(!pending.empty())
            {
                auto [part, next] = std::move(pending.back());
                pending.pop_back();
                while(next < negative.size() && !part.intersects(*negative[next])) ++next;
                if(next != negative.size())
                {
                    auto pieces = std::move(part).remove_slice_carefully(*negative[next]);
                    for(auto &piece : pieces) pending.emplace_back(std::move(piece), next + 1);
                    continue;
                }
                auto candidate = select(part);
                problem = find_checks(candidate, universe);
                if(!problem) result = std::move(candidate);
                return true;
            }
            return false;
        }, work);
        if(result) { ++space.local_resume_hits; return result; }
        if(!problem) return std::nullopt;
        publish(*problem);
    }
}

template<HCOrdering Ordering>
std::optional<point> HC_info::find_witness(
    completion_space &space, const HC &restriction, const Ordering &order, completion_query *query) const
{
    assert(restriction.dimension() == dimension());
    if(restriction.empty()) return std::nullopt;
    ++space.witness_calls;
    space.scratch.reuse_between_points = true;
    space.scratch.count_matching = true;
    // A validated class fixes null entries, jump partners and source/destination
    // timelines. Like local_search(), its other completions need only checks.
    // The shared forest excludes only illegal points, so a legal class member
    // can be returned without traversing that index again.
    if(space.reuse_local)
    {
        const auto try_class = [&](const std::shared_ptr<local_space> &local) -> std::optional<point> {
            auto candidate = local_witness(*local, local->representative, restriction, order);
            // Check exclusions belong to this guarded matching class. Its
            // projected forest retains them without fragmenting the global one.
            local->problems.clear();
            if(candidate)
            {
                ++space.local_hits;
                if(query) query->local = local;
            }
            return candidate;
        };
        if(query && query->local)
            if(auto candidate = try_class(query->local)) return candidate;
        if(space.local && (!query || query->local != space.local))
            if(auto candidate = try_class(space.local)) return candidate;
        for(auto it = space.local_cache.rbegin(); it != space.local_cache.rend(); ++it)
        {
            if(query && query->local == *it) continue;
            if(auto candidate = try_class(*it)) return candidate;
        }
    }
    HC selection = restriction;
    if(!constrain_domain(selection, &space.propagation)) return std::nullopt;
    std::vector<slice> additional_problems;
    std::size_t temporary_regions = 0;
    constexpr std::size_t matching_probe_threshold = 1024;
    for(;;)
    {
        additional_problems.clear();
        std::optional<point> result;
        std::optional<slice> problem;
        const auto visit = [&](const HC &bounds, std::span<const slice *const> negative) {
            for(index_t n = 0; n < dimension(); ++n)
                if(!bounds[n].intersects(selection[n])) return false;
            HC region = bounds;
            for(std::size_t n = 0; n < dimension(); ++n) region[n] &= selection[n];
            if(!prune_slice_refs(region, negative)) return false;
            bool from_local = false;
            const auto select = [&](HC &domain) -> std::optional<point> {
                if(space.reuse_local && space.local)
                    if(auto candidate = take_local_point(*space.local, space.local->representative, domain, order))
                    {
                        from_local = true;
                        return candidate;
                    }
                from_local = false;
                return take_point(domain, order, &space.scratch);
            };
            auto candidate = select(region);
            if(!candidate || !prune_slice_refs(region, negative)) return false;
            if(!region.contains(*candidate) || std::any_of(negative.begin(), negative.end(),
                [&](const slice *p) { return p->contains(*candidate); }))
            {
                // Keep hard-negative subtraction temporary until Deferred's
                // balance rule chooses a persistent cut in the shared forest.
                std::vector<std::pair<HC, std::size_t>> pending;
                pending.emplace_back(std::move(region), 0);
                candidate.reset();
                while(!pending.empty())
                {
                    auto [part, next] = std::move(pending.back());
                    pending.pop_back();
                    while(next < negative.size() && !part.intersects(*negative[next])) ++next;
                    if(next == negative.size())
                    {
                        if(auto chosen = select(part)) { candidate = std::move(chosen); break; }
                        continue;
                    }
                    // Once this query has substantial subdivision work, test
                    // matching before making more descendants of a region.
                    // Failure rules out the entire region. Successful probes
                    // also remove unsupported arrivals before the next cut.
                    if(temporary_regions < matching_probe_threshold) ++temporary_regions;
                    else if(!select(part)) continue;
                    auto pieces = std::move(part).remove_slice_carefully(*negative[next]);
                    for(auto &piece : pieces) pending.emplace_back(std::move(piece), next + 1);
                }
            }
            if(!candidate) return false;
            problem = from_local ? find_checks(*candidate, universe)
                                 : find_problem(*candidate, universe, &additional_problems);
            if(!problem)
            {
                if(space.reuse_local && !from_local)
                {
                    std::shared_ptr<local_space> next;
                    if(space.local && local_contains(*space.local, *candidate)) next = space.local;
                    else
                        for(const auto &cached : space.local_cache)
                            if(local_contains(*cached, *candidate)) { next = cached; break; }
                    if(!next)
                    {
                        next = build_local_space(universe, *candidate);
                        if(next) next->representative = *candidate;
                    }
                    if(next) std::erase(space.local_cache, next);
                    if(space.local && space.local_capacity && next != space.local)
                    {
                        if(space.local_cache.size() >= space.local_capacity) space.local_cache.erase(space.local_cache.begin());
                        space.local_cache.push_back(std::move(space.local));
                    }
                    space.local = std::move(next);
                }
                if(query && space.local) query->local = space.local;
                result = std::move(candidate);
            }
            return true;
        };
        ++space.forest_searches;
        if(query) space.cells.visit(query->choices, restriction, visit, space.scratch.matching_calls);
        else space.cells.visit(restriction, visit, space.scratch.matching_calls);
        if(result || !problem) return result;
        const auto publish = [&](slice exclusion) {
            std::size_t codimension = 0;
            for(const auto &[axis, values] : exclusion.get_fixed_axes())
                codimension += !universe[axis].is_subset_of(values);
            if(codimension <= 1) space.cells.remove_slice_globally(exclusion);
            else
            {
                space.cells.add_negative(std::move(exclusion));
                space.cells.balance(universe);
            }
        };
        publish(std::move(*problem));
        for(auto &related : additional_problems) publish(std::move(related));
    }
}
