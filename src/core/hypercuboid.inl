
#include <algorithm>
#include <cassert>
#include <limits>
#include "graph.h"

template<HCOrdering Ordering>
std::optional<point> HC_info::match_point(
    HC &hc, const Ordering &order, workspace &ws, bool reuse_matching) const
{
    assert(hc.dimension() == dimension());
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
    const auto project_problem = [&](const slice &problem) -> std::optional<slice> {
        slice projected;
        for(const auto &[axis, values] : problem.get_fixed_axes())
        {
            const auto found = std::lower_bound(local.axes.begin(), local.axes.end(), axis);
            index_t n;
            integer_set allowed;
            if(found == local.axes.end() || *found != axis)
            {
                const auto departure = std::find_if(departures.begin(), departures.end(),
                    [&](const local_departure &p) { return p.axis == axis; });
                if(departure == departures.end())
                {
                    if(!values.contains(seed[axis])) return std::nullopt;
                    continue;
                }
                if(departure->values.is_subset_of(values)) continue;
                n = departure->variable;
                for(index_t i : local.universe[n])
                    if(values.contains(std::get<arriving_entry>(axis_coords[local.axes[n]][i]).idx))
                        allowed.insert(i);
            }
            else
            {
                n = static_cast<index_t>(found - local.axes.begin());
                if(local.universe[n].is_subset_of(values)) continue;
                allowed = local.universe[n] & values;
            }
            // Both original axes may restrict the same paired local variable.
            const auto previous = projected.get_fixed_axes().find(n);
            if(previous != projected.get_fixed_axes().end()) allowed &= previous->second;
            if(allowed.empty()) return std::nullopt;
            projected.fix_axis(n, std::move(allowed));
        }
        return projected;
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
        if constexpr(Policy == hc_policy::deferred)
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
        if constexpr(Policy == hc_policy::deferred)
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
    workspace ws;
    ws.reuse_between_points = Policy == hc_policy::deferred;
    std::vector<slice> negative;
    std::size_t num_problems = 0;
    constexpr std::size_t adaptive_problem_limit = 100;
    const auto codimension = [&](const slice &problem) {
        index_t result = 0;
        for(const auto &[axis, values] : problem.get_fixed_axes())
            result += !universe[axis].is_subset_of(values);
        return result;
    };
    while(!ss.empty())
    {
        if constexpr(Policy == hc_policy::balanced)
            if(num_problems == adaptive_problem_limit) break;
        HC hc = std::move(ss.back());
        ss.pop_back();
        if constexpr(Policy == hc_policy::deferred)
            if(!prune_retained_slices(hc, negative)) continue;
        auto pt = take_point(hc, order, &ws);
        if constexpr(Policy == hc_policy::deferred)
        {
            if(!pt || !prune_retained_slices(hc, negative)) continue;
            if(std::any_of(negative.begin(), negative.end(),
                [&](const slice &problem) { return problem.contains(*pt); }))
                pt = take_point(hc, order, &ws, negative);
        }
        if(!pt) continue;
        auto problem = [&] {
            if constexpr(Policy == hc_policy::stable || Policy == hc_policy::deferred)
                return find_problem(*pt, universe);
            else return find_problem(*pt, hc);
        }();
        if(!problem)
        {
            co_yield to_action(*pt);
            if(auto local = build_local_space(hc, *pt))
            {
                auto actions = local_search<Ordering, Policy>(hc, *local, *pt, order, true, negative);
                while(auto moves = actions.first()) co_yield std::move(*moves);
                ss.concat(std::move(hc).remove_slice(local->outer_region));
                for(const slice &problem : local->problems) ss.remove_slice_globally(problem);
            }
            else ss.concat(std::move(hc).remove_point(*pt));
            continue;
        }
        if constexpr(Policy == hc_policy::iterative)
        {
            ss.concat(std::move(hc).remove_slice(*problem));
        }
        else
        {
            ss.push_back(std::move(hc));
            if constexpr(Policy == hc_policy::balanced)
            {
                ++num_problems;
                ss.remove_slice_backwards(*problem, true, 10);
            }
            else if constexpr(Policy == hc_policy::stable)
                ss.remove_slice_globally(*problem);
            else
            {
                if(codimension(*problem) <= 1)
                {
                    ss.remove_slice_globally(*problem);
                    continue;
                }
                negative.push_back(std::move(*problem));
                if(negative.size() > ss.size())
                {
                    const auto cheapest = std::min_element(negative.begin(), negative.end(),
                        [&](const slice &a, const slice &b) { return codimension(a) < codimension(b); });
                    ss.remove_slice_globally(*cheapest);
                    negative.erase(cheapest);
                }
            }
        }
    }
    if constexpr(Policy == hc_policy::balanced)
        for(auto &&moves : search<Ordering, hc_policy::stable>(std::move(ss), std::move(order)))
            co_yield std::move(moves);
}
