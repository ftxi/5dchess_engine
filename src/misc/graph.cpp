#include "graph.h"
#include <queue>
#include <sstream>
#include <algorithm>
#include <cassert>

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

// maintain a tree of pathnodes with
// pn[previous].a ... pn[previous].b -- a ... b
// --- means connected, ... means not connected
struct pathnode
{
    index_t a; // first vertex
    index_t b; // second vertex (not matched to the first one)
    index_t previous; // previous pathnode
};

std::optional<std::vector<std::pair<index_t, index_t>>> graph::find_matching(std::vector<index_t> &include) const
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
        // otherwise, try to find a augumentation path starting from n
        std::vector<bool> seen = adj[n];
        seen[n] = true; // seen will be some nodes of odd distance from n (and n itself)
        std::vector<pathnode> pn = {{n_vertices, n_vertices, n_vertices}}; // pn[0] is a placeholder
        pn.reserve(n_vertices);
        std::queue<index_t> q; // maintain a queue for BFS search
        for (index_t m : neighbors(n))
        {
            index_t index = static_cast<index_t>(pn.size());
            pn.push_back({.a=n, .b=m, .previous=0});
            q.push(index);
        }
        index_t augpathend = n_vertices;
        while(!q.empty())
        {
            index_t index = q.front();
            pathnode p = pn[index];
            q.pop();
            auto us = matched.neighbors(p.b);
//            print_range("Was:", us);
//            std::erase(us, p.a);
//            index_t current_index = p.previous;
//            while(pn[current_index].previous != n_vertices)
//            {
//                std::erase(us, pn[current_index].a);
//                std::erase(us, pn[current_index].a);
//                current_index = pn[current_index].previous;
//            }
//            print_range("Now:", us);
            if(us.empty())
            {
                //if p.b is not matched, we can stop our augmentation path here
                augpathend = index;
                break;
            }
            else
            {
                // otherwise, p.b is matched to some u (the new a)
                index_t u = us[0];
                if(!must_include[u])
                {
                    // if we don't have to include u, we can just drop the match p.b -- u
                    matched.remove_edge(p.b, u);
                    augpathend = index;
                    break;
                }
                else
                {
                    // in the last case, continue the bfs search for all possible v (the new b)
                    seen[p.a] = true;
                    index_t current_index = p.previous;
                    while(pn[current_index].previous != n_vertices)
                    {
                        seen[pn[current_index].a] = true;
                        seen[pn[current_index].b] = true;
                        current_index = pn[current_index].previous;
                    }
                    for(index_t v : neighbors(u))
                    {
                        if(!seen[v])
                        {
                            index_t new_index = static_cast<index_t>(pn.size());
                            pn.push_back({.a=u, .b=v, .previous=index});
                            q.push(new_index);
                            seen[v] = true;
                        }
                    }
                }
            }
        }
        // if we have not found the augmentation path, there is no match
        if(augpathend == n_vertices)
            return std::nullopt;
        // otherwise, modify the matching by takeing symmetric difference
        dprint("found augmenting path:");
        while(augpathend != 0)
        {
            pathnode p = pn[augpathend];
            dprint(p.b, "...", p.a, "---");
            matched.add_edge(p.a, p.b);
            if(p.previous != 0)
            {
                matched.remove_edge(pn[p.previous].b, p.a);
            }
            augpathend = p.previous;
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
