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
graph::find_matching(const std::vector<index_t> &include) const
{
    dprint("finding a match on" + to_string());
    // With no mandatory vertex, the empty matching is already a solution.
    // This is also the common take_point() case, so avoid constructing the
    // alternating-path workspace until it is actually needed.
    if(include.empty())
    {
        return std::vector<std::pair<index_t, index_t>>{};
    }

    std::vector<index_t> mate(n_vertices, nil_vertex);
    std::vector<std::uint8_t> must_include(n_vertices, false);
    for(index_t n : include)
    {
        must_include[n] = true;
    }

    // Storage used by every augmenting-path search in this matching. Keeping it
    // here makes its lifetime explicit and avoids reallocating it for each root.
    // the BFS search tree structure
    std::vector<index_t> parent(n_vertices);
    // base[v] is the base of the blossom containing v, or v itself if v is not in a blossom
    std::vector<index_t> base(n_vertices);
    // whether a vertex is reachable from the root by an even-length alternating path
    std::vector<std::uint8_t> is_even_reachable(n_vertices);
    // bases included in the blossom currently being contracted
    std::vector<std::uint8_t> in_blossom(n_vertices);
    // used to find the lowest common ancestor of two vertices in the alternating tree
    std::vector<std::uint8_t> in_ancestor_path(n_vertices);
    // the BFS queue
    std::vector<index_t> queue;
    // the returned path and temporary storage used to reconstruct it
    std::vector<index_t> path;
    std::vector<index_t> reverse_path;
    queue.reserve(n_vertices);
    path.reserve(n_vertices);
    reverse_path.reserve(n_vertices);

    const auto find_augmenting_path = [&](index_t root) {
        std::fill(parent.begin(), parent.end(), nil_vertex);
        std::iota(base.begin(), base.end(), index_t{0});
        std::fill(is_even_reachable.begin(), is_even_reachable.end(), false);
        queue.clear();
        path.clear();
        reverse_path.clear();

        const auto lowest_common_ancestor = [&](index_t a, index_t b) {
            std::fill(in_ancestor_path.begin(), in_ancestor_path.end(), false);
            while(true)
            {
                a = base[a];
                in_ancestor_path[a] = true;
                if(mate[a] == nil_vertex)
                {
                    break;
                }
                a = parent[mate[a]];
            }
            while(true)
            {
                b = base[b];
                if(in_ancestor_path[b])
                {
                    return b;
                }
                b = parent[mate[b]];
            }
        };

        const auto mark_blossom_path = [&](index_t vertex, index_t blossom_base, index_t child) {
            while(base[vertex] != blossom_base)
            {
                in_blossom[base[vertex]] = true;
                in_blossom[base[mate[vertex]]] = true;
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
        is_even_reachable[root] = true;
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

                // Our path may end after a matching edge at a vertex outside S.
                // Check that endpoint before comparing blossom bases: contraction
                // may put the endpoint inside the current blossom while parent[]
                // still describes the path that reaches it.
                if(mate[neighbor] != nil_vertex && !must_include[mate[neighbor]])
                {
                    // If no route reaches neighbor in the odd role yet, record
                    // the nonmatching edge from vertex.
                    if(parent[neighbor] == nil_vertex)
                    {
                        // if neighbor and vertex are in the same blossom, skip
                        if(base[vertex] == base[neighbor])
                        {
                            continue;
                        }
                        parent[neighbor] = vertex;
                    }
                    // otherwise, the path ends after neighbor's matching edge, at its
                    // optional mate outside S.
                    reconstruct_path(neighbor);
                    path.push_back(mate[neighbor]);
                    return true;
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
                    std::fill(in_blossom.begin(), in_blossom.end(), false);
                    mark_blossom_path(vertex, blossom_base, neighbor);
                    mark_blossom_path(neighbor, blossom_base, vertex);
                    // for the contracted blossom
                    for(index_t v = 0; v < n_vertices; ++v)
                    {
                        if(in_blossom[base[v]])
                        {
                            base[v] = blossom_base;
                            // since a blossom is an odd cycle, all vertices in it are even-reachable
                            // even those initially thought as odd (take another path through the blossom)
                            // they will need to be added to the searching queue
                            if(!is_even_reachable[v])
                            {
                                is_even_reachable[v] = true;
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
                    is_even_reachable[next] = true;
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

    std::vector<std::pair<index_t, index_t>> result;
    result.reserve(include.size());
    for(index_t vertex = 0; vertex < n_vertices; ++vertex)
    {
        if(mate[vertex] < vertex)
        {
            result.emplace_back(vertex, mate[vertex]);
        }
    }
    return result;
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
