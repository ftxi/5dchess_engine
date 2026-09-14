#ifndef GRAPH_H
#define GRAPH_H

#include <cstddef>
#include <optional>
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
    explicit graph(index_t n)
        : n_vertices{n},
          adj(n)
    {}

    void add_edge(index_t u, index_t v);

    std::optional<std::vector<std::pair<index_t, index_t>>>
        find_matching(const std::vector<index_t> &include) const;

    std::string to_string() const;
};

#endif /* GRAPH_H */
