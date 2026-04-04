#include <string>
#include <optional>
#include <vector>
#include <cassert>


class graph
{
    // undirected graph of n vertices
    // represented as an adjacency matrix
    uint32_t n_vertices;
    std::vector<std::vector<bool>> adj;
public:
    graph(uint32_t n): n_vertices{n}
    {
        adj.resize(n);
        for(uint32_t i = 0; i < n; i++)
        {
            adj[i].resize(n, false);
        }
    }
    void add_edge(uint32_t u, uint32_t v);
    void remove_edge(uint32_t u, uint32_t v);
    bool not_isolated(uint32_t u) const;
    std::vector<uint32_t> neighbors(uint32_t u) const;
    
    std::optional<std::vector<std::pair<uint32_t,uint32_t>>> find_matching(std::vector<uint32_t>& include) const;
    
    std::string to_string() const;
};
