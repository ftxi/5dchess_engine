#include "graph.h"
#include <queue>
#include <sstream>
#include <algorithm>
#include <cassert>

#include <iostream>

void graph::add_edge(uint32_t u, uint32_t v)
{
    assert(u!=v && "loops are not allowed");
    adj[u][v] = true;
    adj[v][u] = true;
}

void graph::remove_edge(uint32_t u, uint32_t v)
{
    adj[u][v] = false;
    adj[v][u] = false;
}


bool graph::not_isolated(uint32_t u) const
{
    for(bool b : adj[u])
    {
        if(b) return true;
    }
    return false;
}

std::vector<uint32_t> graph::neighbors(uint32_t u) const
{
    std::vector<uint32_t> result;
    result.reserve(n_vertices);
    for(uint32_t v = 0; v < n_vertices; v++)
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
    uint32_t a; // first vertex
    uint32_t b; // second vertex (not matched to the first one)
    uint32_t previous; // previous pathnode
};

std::optional<std::vector<std::pair<uint32_t, uint32_t>>> graph::find_matching(std::vector<uint32_t> &include) const
{
    graph matched(n_vertices);
    std::vector<bool> must_include(n_vertices, false);
    for(uint32_t n : include)
    {
        must_include[n] = true;
    }
    for(uint32_t n : include)
    {
        //std::cerr << "n=" << n << "\n";
        // if n is already matched, skip
        if(matched.not_isolated(n)) continue;
        // otherwise, try to find a augumentation path starting from n
        std::vector<bool> seen = adj[n];
        seen[n] = true; // seen will be some nodes of odd distance from n (and n itself)
        std::vector<pathnode> pn = {{n_vertices, n_vertices, n_vertices}}; // pn[0] is a placeholder
        pn.reserve(n_vertices);
        std::queue<uint32_t> q; // maintan a queue for BFS search
        for (uint32_t m : neighbors(n))
        {
            uint32_t index = static_cast<uint32_t>(pn.size());
            pn.push_back({.a=n, .b=m, .previous=0});
            q.push(index);
        }
        uint32_t augpathend = 0;
        while(!q.empty() && augpathend==0)
        {
            uint32_t index = q.front();
            pathnode p = pn[index];
            q.pop();
            auto us = matched.neighbors(p.b);
            if(us.empty())
            {
                //if p.b is not matched, we can stop our augmentation path here
                augpathend = index;
                break;
            }
            else
            {
                // otherwise, p.b is matched to some u (the new a)
                uint32_t u = us[0];
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
                    for(uint32_t v : neighbors(u))
                    {
                        if(!seen[v])
                        {
                            uint32_t new_index = static_cast<uint32_t>(pn.size());
                            pn.push_back({.a=u, .b=v, .previous=index});
                            q.push(new_index);
                            seen[v] = true;
                        }
                    }
                }
            }
        }
        // if we have not found the augmentation path, there is no match
        if(augpathend == 0)
            return std::nullopt;
        // otherwise, modify the matching by takeing symmetric difference
        //std::cerr << "found augmenting path: ";
        while(augpathend != 0)
        {
            pathnode p = pn[augpathend];
            //std::cerr << p.b << "..." << p.a << "---";
            matched.add_edge(p.a, p.b);
            if(p.previous != 0)
            {
                matched.remove_edge(pn[p.previous].b, p.a);
            }
            augpathend = p.previous;
        }
        //std::cerr << matched.to_string();
    }
    std::vector<std::pair<uint32_t,uint32_t>> result;
    for(uint32_t i = 0; i < n_vertices; i++)
    {
        for(uint32_t j = 0; j < i; j++)
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
        for (uint32_t i = 0; i < n_vertices; ++i)
            oss << (i ? "," : "") << i;
        oss << "}";
    } else {
        oss << "{0,1,...," << (n_vertices - 1) << "}";
    }
    oss << " and edges:\n";
    for(uint32_t i = 0; i < n_vertices; i++)
    {
//        if(std::any_of(adj[i].begin(), adj[i].begin()+i, [](bool x){return x;}))
//        {
//            oss << i << " -- ";
//            for(int j = 0; j < i; j++)
//            {
//                if(adj[i][j])
//                {
//                    oss << j << ", ";
//                }
//            }
//            oss << "\n";
//        }
        for(uint32_t j = 0; j < n_vertices; j++)
        {
            oss << adj[i][j];
        }
        oss << "\n";
    }
    return oss.str();
}
