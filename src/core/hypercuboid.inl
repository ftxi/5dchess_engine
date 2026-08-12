
#include <cassert>
#include <limits>
#include "graph.h"

template<HC_ordering Order>
std::optional<point> HC_info::take_point(HC &hc, const Order &order) const
{
    assert(hc.dimension() == dimension);
    graph g(dimension);
    std::vector<index_t> must_include;
    constexpr index_t invalid_index = std::numeric_limits<index_t>::max();

    const size_t num_edges = static_cast<std::size_t>(dimension) * dimension;
    std::vector<index_t> edge_refs(num_edges, invalid_index);
    const auto edge_ref = [&](index_t p, index_t q) -> index_t & {
        return edge_refs[static_cast<std::size_t>(p) * dimension + q];
    };

    point result(dimension, invalid_index);
    for(index_t n = 0; n < dimension; n++)
    {
        bool has_nonjump = false;
        integer_set ghost_arrive_indices;
        order.for_each(n, hc[n], [&](index_t i) {
            const entry &loc = axis_coords[n][i];
            std::visit(overloads {
                [&](const physical_entry&) {
                    if(!has_nonjump)
                    {
                        has_nonjump = true;
                        result[n] = i;
                    }
                },
                [&](const arriving_entry &arriving) {
                    index_t from_axis = line_to_axis.at(arriving.m.from.l());
                    if(!hc[from_axis].contains(arriving.idx))
                    {
                        ghost_arrive_indices.insert(i);
                        return;
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
                },
                [](const departing_entry&) {},
                [&](const null_entry&) {
                    if(!has_nonjump)
                    {
                        has_nonjump = true;
                        result[n] = i;
                    }
                },
            }, loc);
        });

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

    auto matching = g.find_matching(must_include);
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
#ifndef NDEBUG
    for(index_t i : result)
    {
        assert(i != invalid_index && "some axis is still null");
    }
#endif
    assert(hc.contains(result));
    return result;
}

template<HC_ordering Order>
generator<moveseq> HC_info::iterative_search(search_space ss, Order order) const
{
    while(!ss.empty())
    {
        HC hc = ss.back();
        ss.pop_back();
        auto pt_opt = take_point(hc, order);
        if(!pt_opt)
        {
            continue;
        }

        point pt = *pt_opt;
        auto problem = find_problem(pt, hc);
        if(problem)
        {
            ss.concat(hc.remove_slice(*problem));
        }
        else
        {
            co_yield to_action(pt);
            ss.concat(hc.remove_point(pt));
        }
    }
}

template<HC_ordering Order>
generator<moveseq> HC_info::search(search_space ss, Order order) const
{
    while(!ss.empty())
    {
        HC hc = ss.back();
        ss.pop_back();
        auto pt_opt = take_point(hc, order);
        if(!pt_opt)
        {
            continue;
        }

        point pt = *pt_opt;
        auto problem = find_problem(pt, hc);
        if(!problem)
        {
            co_yield to_action(pt);
            ss.concat(hc.remove_point(pt));
            continue;
        }

        const slice &problem_slice = *problem;
        search_space adjoined;
        adjoined.concat(hc.remove_slice(problem_slice));
        int intersect_count = 1;
        int disjoint_count = 0;
        while(!ss.empty() && disjoint_count * 10 < intersect_count)
        {
            HC &other_hc = ss.back();
            if(other_hc.intersects(problem_slice))
            {
                adjoined.concat(other_hc.remove_slice_if_good(problem_slice, 1));
                intersect_count++;
            }
            else
            {
                disjoint_count++;
                adjoined.concat({{other_hc}});
            }
            ss.pop_back();
        }
        ss.concat(std::move(adjoined));
    }
}

