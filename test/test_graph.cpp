#include "graph.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace
{
using adjacency_matrix = std::vector<std::vector<bool>>;
using matching = std::vector<std::pair<index_t, index_t>>;

void require(bool condition)
{
    if(!condition)
    {
        std::abort();
    }
}

bool brute_force_match(
    const adjacency_matrix &adjacency,
    const std::vector<bool> &required,
    std::vector<bool> &matched)
{
    std::size_t vertex = 0;
    while(vertex < required.size()
          && (!required[vertex] || matched[vertex]))
    {
        ++vertex;
    }
    if(vertex == required.size())
    {
        return true;
    }

    matched[vertex] = true;
    for(std::size_t other = 0; other < adjacency.size(); ++other)
    {
        if(adjacency[vertex][other] && !matched[other])
        {
            matched[other] = true;
            if(brute_force_match(adjacency, required, matched))
            {
                matched[vertex] = false;
                matched[other] = false;
                return true;
            }
            matched[other] = false;
        }
    }
    matched[vertex] = false;
    return false;
}

bool brute_force_match(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required_vertices)
{
    std::vector<bool> required(adjacency.size(), false);
    for(index_t vertex : required_vertices)
    {
        required[vertex] = true;
    }
    std::vector<bool> matched(adjacency.size(), false);
    return brute_force_match(adjacency, required, matched);
}

void validate_matching(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required,
    const matching &result)
{
    std::vector<bool> matched(adjacency.size(), false);
    for(const auto [u, v] : result)
    {
        require(u < adjacency.size());
        require(v < adjacency.size());
        require(u != v);
        require(adjacency[u][v]);
        require(!matched[u]);
        require(!matched[v]);
        matched[u] = true;
        matched[v] = true;
    }
    for(index_t vertex : required)
    {
        require(matched[vertex]);
    }
}

void compare_with_brute_force(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required)
{
    graph tested(static_cast<index_t>(adjacency.size()));
    for(index_t u = 0; u < adjacency.size(); ++u)
    {
        for(index_t v = 0; v < u; ++v)
        {
            if(adjacency[u][v])
            {
                tested.add_edge(u, v);
            }
        }
    }

    const bool expected = brute_force_match(adjacency, required);
    const std::optional<matching> actual = tested.find_matching(required);
    if(actual.has_value() != expected)
    {
        std::cerr << "matching disagreement: expected=" << expected
                  << ", actual=" << actual.has_value() << "\nrequired:";
        for(index_t vertex : required)
        {
            std::cerr << ' ' << vertex;
        }
        std::cerr << "\nedges:\n";
        for(index_t u = 0; u < adjacency.size(); ++u)
        {
            for(index_t v = 0; v < u; ++v)
            {
                if(adjacency[u][v])
                {
                    std::cerr << u << " -- " << v << '\n';
                }
            }
        }
        std::abort();
    }
    if(actual)
    {
        validate_matching(adjacency, required, *actual);
    }
}

adjacency_matrix make_graph(
    std::size_t size,
    std::initializer_list<std::pair<index_t, index_t>> edges)
{
    adjacency_matrix adjacency(size, std::vector<bool>(size, false));
    for(const auto [u, v] : edges)
    {
        adjacency[u][v] = adjacency[v][u] = true;
    }
    return adjacency;
}

void test_existing_examples()
{
    compare_with_brute_force(make_graph(7, {
        {0, 1}, {1, 2}, {3, 4}, {4, 5}, {5, 6}, {6, 0}
    }), {0, 2, 5, 6});

    compare_with_brute_force(make_graph(10, {
        {2, 1}, {3, 2}, {4, 2}, {5, 0}, {5, 1}, {5, 2}, {5, 3},
        {5, 4}, {6, 3}, {7, 0}, {7, 4}, {8, 0}, {8, 3}, {8, 4}
    }), {0, 1, 2, 5, 6, 7, 8});
}

void test_blossom_regression()
{
    compare_with_brute_force(make_graph(10, {
        {3, 0}, {4, 2}, {5, 0}, {5, 1}, {5, 3}, {6, 2}, {6, 5},
        {7, 0}, {7, 2}, {7, 3}, {7, 4}, {7, 5}, {7, 6}, {8, 3},
        {9, 3}, {9, 4}
    }), {0, 1, 2, 3, 4, 5, 6, 7, 9});
}

void test_257_vertices()
{
    adjacency_matrix adjacency(257, std::vector<bool>(257, false));
    adjacency[255][256] = adjacency[256][255] = true;
    compare_with_brute_force(adjacency, {255, 256});
}

void test_random_graphs()
{
    std::mt19937 random(0x5dca55u);
    std::bernoulli_distribution has_edge(0.35);
    std::bernoulli_distribution is_required(0.55);

    for(index_t dimension = 1; dimension <= 10; ++dimension)
    {
        for(int sample = 0; sample < 200; ++sample)
        {
            adjacency_matrix adjacency(
                dimension, std::vector<bool>(dimension, false));
            for(index_t u = 0; u < dimension; ++u)
            {
                for(index_t v = 0; v < u; ++v)
                {
                    adjacency[u][v] = adjacency[v][u] = has_edge(random);
                }
            }

            std::vector<index_t> required;
            for(index_t vertex = 0; vertex < dimension; ++vertex)
            {
                if(is_required(random))
                {
                    required.push_back(vertex);
                }
            }
            compare_with_brute_force(adjacency, required);
        }
    }
}
}

int main()
{
    test_existing_examples();
    test_blossom_regression();
    test_257_vertices();
    test_random_graphs();
    std::cerr << "---= test_graph.cpp: all passed =---\n";
}
