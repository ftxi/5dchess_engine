#include <string>
#include <optional>
#include <vector>
#include <cassert>
#include "integer_set.h"

class graph
{
    // undirected graph of n vertices
    // represented as an adjacency matrix
    index_t n_vertices;
    std::vector<std::vector<bool>> adj;
public:
    graph(index_t n): n_vertices{n}
    {
        adj.resize(n);
        for(index_t i = 0; i < n; i++)
        {
            adj[i].resize(n, false);
        }
    }
    void add_edge(index_t u, index_t v);
    void remove_edge(index_t u, index_t v);
    bool not_isolated(index_t u) const;
    std::vector<index_t> neighbors(index_t u) const;
    
    std::optional<std::vector<std::pair<index_t,index_t>>> find_matching(std::vector<index_t>& include) const;
    
    std::string to_string() const;
};
