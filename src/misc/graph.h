#ifndef GRAPH_H
#define GRAPH_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "integer_set.h"

class graph
{
    // undirected graph of n vertices
    // represented as one dynamic bit-set per row
    index_t n_vertices;
    std::vector<integer_set> adj;

    [[nodiscard]] bool has_edge(index_t u, index_t v) const
    {
        return adj[u].contains(v);
    }

public:
    // One active call per workspace. Results own their storage independently.
    // Capacity is retained across calls, graphs and dimension changes.
    struct workspace
    {
    private:
        friend class graph;
        std::vector<index_t> mate, parent, base, queue, path, reverse_path;
        integer_set must_include, is_even_reachable, in_blossom, in_ancestor_path;
    };

    explicit graph(index_t n)
        : n_vertices{n},
          adj(n)
    {}

    // Start an empty graph, retaining adjacency storage where possible.
    void reset(index_t n)
    {
        for(auto &row : adj) row.clear();
        adj.resize(n);
        n_vertices = n;
    }

    void add_edge(index_t u, index_t v);

    // Surviving, disjoint seed edges touching required vertices initialize the
    // matching. Missing or out-of-range edges are ignored; coverage is repaired.
    std::optional<std::vector<std::pair<index_t, index_t>>>
        find_matching(const std::vector<index_t> &include, workspace &ws,
            std::span<const std::pair<index_t, index_t>> initial = {}) const;

    std::string to_string() const;
};

#endif /* GRAPH_H */
