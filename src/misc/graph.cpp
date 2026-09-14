#include "graph.h"
#include <queue>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>

#include "debug.h"

void graph::add_edge(index_t u, index_t v)
{
    assert(u!=v && "loops are not allowed");
    adj[u][v] = true;
    adj[v][u] = true;
}

void graph::remove_edge(index_t u, index_t v)
{
    adj[u][v] = false;
    adj[v][u] = false;
}


bool graph::not_isolated(index_t u) const
{
    for(bool b : adj[u])
    {
        if(b) return true;
    }
    return false;
}

std::vector<index_t> graph::neighbors(index_t u) const
{
    std::vector<index_t> result;
    result.reserve(n_vertices);
    for(index_t v = 0; v < n_vertices; v++)
    {
        if(adj[u][v])
        {
            result.push_back(v);
        }
    }
    return result;
}

std::optional<std::vector<index_t>> graph::find_augmenting_path(
    index_t root,
    const graph &matching,
    const std::vector<bool> &must_include
) const
{
    constexpr index_t nil_vertex = std::numeric_limits<index_t>::max();

    // Convert the matching graph to the usual one-mate-per-vertex form used
    // by the alternating-tree search.
    std::vector<index_t> mate(n_vertices, nil_vertex);
    for(index_t u = 0; u < n_vertices; ++u)
    {
        for(index_t v = 0; v < u; ++v)
        {
            if(matching.adj[u][v])
            {
                assert(mate[u] == nil_vertex);
                assert(mate[v] == nil_vertex);
                mate[u] = v;
                mate[v] = u;
            }
        }
    }

    // the BFS search tree structure
    std::vector<index_t> parent(n_vertices, nil_vertex);
    // base[v] is the base of the blossom containing v, or v itself if v is not in a blossom
    std::vector<index_t> base(n_vertices); 
    std::iota(base.begin(), base.end(), index_t{0});
    // whether a vertex is reachable from the root by an even-length alternating path
    std::vector<bool> is_even_reachable(n_vertices, false);
    // bases included in the blossom currently being contracted
    std::vector<bool> in_blossom(n_vertices, false);
    // used to find the lowest common ancestor of two vertices in the alternating tree
    std::vector<bool> in_ancestor_path(n_vertices, false);
    // the BFS queue
    std::queue<index_t> queue;

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
    const auto reconstruct_path = [&] (index_t endpoint) {
        std::vector<index_t> reverse_path;
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
        std::reverse(reverse_path.begin(), reverse_path.end());
        assert(!reverse_path.empty() && reverse_path.front() == root);
        return reverse_path;
    };

    // BFS starts
    queue.push(root);
    is_even_reachable[root] = true;
    while(!queue.empty())
    {
        const index_t vertex = queue.front();
        queue.pop();
        // for each (potential) neighbor of the focused vertex
        // as commented below, if the 'neighbor' is not really a neighbor
        // we will skip it
        for(index_t neighbor = 0; neighbor < n_vertices; ++neighbor)
        {
            // skip nonexistent edges
            // and the matching edge from vertex.
            if(!adj[vertex][neighbor] || mate[vertex] == neighbor)
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
                std::vector<index_t> path = reconstruct_path(neighbor);
                path.push_back(mate[neighbor]);
                return path;
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
                            queue.push(v);
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
                    return reconstruct_path(neighbor);
                }
                // otherwise, add the neighbor's mate to the tree
                // and add it to the searching queue
                const index_t next = mate[neighbor];
                is_even_reachable[next] = true;
                queue.push(next);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::pair<index_t, index_t>>> graph::find_matching(const std::vector<index_t> &include) const
{
    dprint("finding a match on" + to_string());
    graph matched(n_vertices);
    std::vector<bool> must_include(n_vertices, false);
    for(index_t n : include)
    {
        must_include[n] = true;
    }
    for(index_t n : include)
    {
        dprint("n=" + n);
        // if n is already matched, skip
        if(matched.not_isolated(n)) continue;
        // Otherwise, find an augmenting path starting from n. Odd alternating
        // cycles are contracted so reaching a vertex through one side of a
        // blossom does not hide a valid path through the other side.
        const auto path = find_augmenting_path(n, matched, must_include);
        if(!path)
            return std::nullopt;

        // Update the matching by taking the symmetric difference with path.
        // Remove its matching edges first, then add its non-matching edges.
        dprint("found augmenting path:");
        for(std::size_t i = 1; i + 1 < path->size(); i += 2)
        {
            matched.remove_edge((*path)[i], (*path)[i + 1]);
        }
        for(std::size_t i = 0; i + 1 < path->size(); i += 2)
        {
            dprint((*path)[i], "---", (*path)[i + 1]);
            matched.add_edge((*path)[i], (*path)[i + 1]);
        }
        dprint("Updated to:", matched.to_string());
    }
    std::vector<std::pair<index_t,index_t>> result;
    for(index_t i = 0; i < n_vertices; i++)
    {
        for(index_t j = 0; j < i; j++)
        {
            if(matched.adj[i][j])
            {
                result.push_back(std::make_pair(i,j));
            }
        }
    }
    return result;
}

std::string graph::to_string() const
{
    std::ostringstream oss;
    oss << "Graph with vertices "; // << n_vertices << " vertices:\n";
    if (n_vertices <= 3) {
        oss << "{";
        for (index_t i = 0; i < n_vertices; ++i)
            oss << (i ? "," : "") << i;
        oss << "}";
    } else {
        oss << "{0,1,...," << (n_vertices - 1) << "}";
    }
    oss << " and edges:\n";
    for(index_t i = 0; i < n_vertices; i++)
    {
        if(std::any_of(adj[i].begin(), adj[i].begin()+i, [](bool x){return x;}))
        {
            oss << i << " -- ";
            for(index_t j = 0; j < i; j++)
            {
                if(adj[i][j])
                {
                    oss << j << ", ";
                }
            }
            oss << "\n";
        }
//        for(index_t j = 0; j < n_vertices; j++)
//        {
//            oss << adj[i][j];
//        }
        oss << "\n";
    }
    return oss.str();
}
