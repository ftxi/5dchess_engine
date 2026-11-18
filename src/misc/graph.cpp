#include "graph.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>
#include <sstream>

#include "debug.h"

namespace
{
constexpr index_t nil_vertex = std::numeric_limits<index_t>::max();
}

void graph::add_edge(index_t u, index_t v)
{
    assert(u != v && "loops are not allowed");
    adj[u].insert(v);
    adj[v].insert(u);
}

std::optional<std::vector<std::pair<index_t, index_t>>>
graph::find_matching(const std::vector<index_t> &include, workspace &ws,
    std::span<const std::pair<index_t, index_t>> initial) const
{
    dprint("finding a match on" + to_string());
    // With no mandatory vertex, the empty matching is already a solution.
    // This is also the common take_point() case, so do not touch matching
    // scratch until it is actually needed.
    if(include.empty())
    {
        return std::vector<std::pair<index_t, index_t>>{};
    }

    auto &mate = ws.mate;
    mate.assign(n_vertices, nil_vertex);
    auto &must_include = ws.must_include;
    must_include.clear();
    for(index_t n : include) must_include.insert(n);
    const auto collect = [&] {
        std::vector<std::pair<index_t, index_t>> result;
        result.reserve(initial.empty() ? include.size()
            : std::min<std::size_t>(n_vertices / 2, include.size() + initial.size()));
        for(index_t vertex = 0; vertex < n_vertices; ++vertex)
            if(mate[vertex] < vertex) result.emplace_back(vertex, mate[vertex]);
        return result;
    };
    if(!initial.empty())
    {
        // A seed may come from another graph or dimension. Retain only useful
        // surviving edges, without introducing optional-only matching pairs.
        for(const auto &[u, v] : initial)
        {
            if(u >= n_vertices || v >= n_vertices || u == v
                || (!must_include.contains(u) && !must_include.contains(v))
                || !has_edge(u, v)
                || mate[u] != nil_vertex || mate[v] != nil_vertex) continue;
            mate[u] = v;
            mate[v] = u;
        }
        if(std::all_of(include.begin(), include.end(),
            [&](index_t vertex) { return mate[vertex] != nil_vertex; })) return collect();
    }
    // Prepare path-search storage only when the seed needs repair. Retain its
    // capacity across calls, but initialize logical state for each new root.
    // the BFS search tree structure
    auto &parent = ws.parent;
    parent.resize(n_vertices);
    // base[v] is the base of the blossom containing v, or v itself if v is not in a blossom
    auto &base = ws.base;
    base.resize(n_vertices);
    // whether a vertex is reachable from the root by an even-length alternating path
    auto &is_even_reachable = ws.is_even_reachable;
    // bases included in the blossom currently being contracted
    auto &in_blossom = ws.in_blossom;
    // used to find the lowest common ancestor of two vertices in the alternating tree
    auto &in_ancestor_path = ws.in_ancestor_path;
    // the BFS queue
    auto &queue = ws.queue;
    // the returned path and temporary storage used to reconstruct it
    auto &path = ws.path;
    auto &reverse_path = ws.reverse_path;
    queue.reserve(n_vertices);
    path.reserve(n_vertices);
    reverse_path.reserve(n_vertices);

    const auto find_augmenting_path = [&](index_t root) {
        std::fill(parent.begin(), parent.end(), nil_vertex);
        std::iota(base.begin(), base.end(), index_t{0});
        is_even_reachable.clear();
        queue.clear();
        path.clear();
        reverse_path.clear();

        const auto lowest_common_ancestor = [&](index_t a, index_t b) {
            in_ancestor_path.clear();
            while(true)
            {
                a = base[a];
                in_ancestor_path.insert(a);
                if(mate[a] == nil_vertex)
                {
                    break;
                }
                a = parent[mate[a]];
            }
            while(true)
            {
                b = base[b];
                if(in_ancestor_path.contains(b))
                {
                    return b;
                }
                b = parent[mate[b]];
            }
        };

        const auto mark_blossom_path = [&](index_t vertex, index_t blossom_base, index_t child) {
            while(base[vertex] != blossom_base)
            {
                in_blossom.insert(base[vertex]);
                in_blossom.insert(base[mate[vertex]]);
                parent[vertex] = child;
                child = mate[vertex];
                vertex = parent[mate[vertex]];
            }
        };

        // Reconstruct root ... endpoint from the alternating-tree parents.
        // The parent adjustments made while contracting a blossom expand it into the
        // correct alternating path here.
        const auto reconstruct_path = [&](index_t endpoint) {
            reverse_path.clear();
            index_t vertex = endpoint;
            while(vertex != nil_vertex)
            {
                reverse_path.push_back(vertex);
                const index_t previous = parent[vertex];
                if(previous == nil_vertex)
                {
                    break;
                }
                reverse_path.push_back(previous);
                vertex = mate[previous];
            }
            path.assign(reverse_path.rbegin(), reverse_path.rend());
            assert(!path.empty() && path.front() == root);
        };

        // BFS starts
        queue.push_back(root);
        is_even_reachable.insert(root);
        std::size_t queue_position = 0;
        while(queue_position < queue.size())
        {
            const index_t vertex = queue[queue_position++];
            // for each neighbor of the focused vertex
            for(index_t neighbor : adj[vertex])
            {
                // skip the matching edge from vertex.
                if(mate[vertex] == neighbor)
                {
                    continue;
                }

                // if the edge is internal to a blossom, skip
                if(base[vertex] == base[neighbor])
                {
                    continue;
                }

                // if the current edge closes an odd alternating cycle.
                const bool closes_blossom = neighbor == root
                    || (mate[neighbor] != nil_vertex
                        && parent[mate[neighbor]] != nil_vertex);
                if(closes_blossom)
                {
                    // mark the blossom
                    const index_t blossom_base = lowest_common_ancestor(vertex, neighbor);
                    in_blossom.clear();
                    mark_blossom_path(vertex, blossom_base, neighbor);
                    mark_blossom_path(neighbor, blossom_base, vertex);
                    // for the contracted blossom
                    for(index_t v = 0; v < n_vertices; ++v)
                    {
                        if(in_blossom.contains(base[v]))
                        {
                            base[v] = blossom_base;
                            // since a blossom is an odd cycle, all vertices in it are even-reachable
                            // even those initially thought as odd (take another path through the blossom)
                            // they will need to be added to the searching queue
                            if(!is_even_reachable.contains(v))
                            {
                                // A newly even-reachable vertex outside S is a valid endpoint.
                                if(!must_include.contains(v))
                                {
                                    reconstruct_path(mate[v]);
                                    path.push_back(v);
                                    return true;
                                }
                                is_even_reachable.insert(v);
                                queue.push_back(v);
                            }
                        }
                    }
                }
                // if the neighbor is not in the tree, add it
                else if(parent[neighbor] == nil_vertex)
                {
                    parent[neighbor] = vertex;
                    // if the neighbor is unmatched, we found an augmenting path
                    if(mate[neighbor] == nil_vertex)
                    {
                        reconstruct_path(neighbor);
                        return true;
                    }
                    // otherwise, add the neighbor's mate to the tree
                    // and add it to the searching queue
                    const index_t next = mate[neighbor];
                    if(!must_include.contains(next))
                    {
                        reconstruct_path(neighbor);
                        path.push_back(next);
                        return true;
                    }
                    is_even_reachable.insert(next);
                    queue.push_back(next);
                }
            }
        }
        return false;
    };

    for(index_t n : include)
    {
        dprint("n=", n);
        // if n is already matched, skip
        if(mate[n] != nil_vertex) continue;
        // Otherwise, find an augmenting path starting from n. Odd alternating
        // cycles are contracted so reaching a vertex through one side of a
        // blossom does not hide a valid path through the other side.
        if(!find_augmenting_path(n))
        {
            return std::nullopt;
        }

        // Update the matching by taking the symmetric difference with path.
        // Remove its matching edges first, then add its non-matching edges.
        dprint("found augmenting path:");
        for(std::size_t i = 1; i + 1 < path.size(); i += 2)
        {
            assert(mate[path[i]] == path[i + 1]);
            assert(mate[path[i + 1]] == path[i]);
            mate[path[i]] = nil_vertex;
            mate[path[i + 1]] = nil_vertex;
        }
        for(std::size_t i = 0; i + 1 < path.size(); i += 2)
        {
            dprint(path[i], "---", path[i + 1]);
            assert(mate[path[i]] == nil_vertex);
            assert(mate[path[i + 1]] == nil_vertex);
            mate[path[i]] = path[i + 1];
            mate[path[i + 1]] = path[i];
        }
    }

    return collect();
}

std::string graph::to_string() const
{
    std::ostringstream oss;
    oss << "Graph with vertices ";
    if(n_vertices <= 3)
    {
        oss << "{";
        for(index_t i = 0; i < n_vertices; ++i)
        {
            oss << (i ? "," : "") << i;
        }
        oss << "}";
    }
    else
    {
        oss << "{0,1,...," << (n_vertices - 1) << "}";
    }
    oss << " and edges:\n";
    for(index_t i = 0; i < n_vertices; ++i)
    {
        bool has_previous_neighbor = false;
        for(index_t j = 0; j < i; ++j)
        {
            has_previous_neighbor |= has_edge(i, j);
        }
        if(has_previous_neighbor)
        {
            oss << i << " -- ";
            for(index_t j = 0; j < i; ++j)
            {
                if(has_edge(i, j))
                {
                    oss << j << ", ";
                }
            }
            oss << "\n";
        }
        oss << "\n";
    }
    return oss.str();
}
