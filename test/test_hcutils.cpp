#include <iostream>
#include "graph.h"
#include "hypercuboid.h"

void test_graph()
{
    graph g(7);
    g.add_edge(0,1);
    g.add_edge(1,2);
    g.add_edge(3,4);
    g.add_edge(4,5);
    g.add_edge(5,6);
    g.add_edge(6,0);
    std::cout << g.to_string();

    std::vector<index_t> include = {0,2,5,6};
    auto m = g.find_matching(include);
    if(m)
    {
        std::cout << "Found matching:\n";
        for(auto p : m.value())
        {
            std::cout << p.first << " -- " << p.second << "\n";
        }
    }
    else
    {
        std::cout << "No matching found\n";
    }
}

void test_discrete_graph()
{
    graph g(2);
    std::cout << g.to_string();

    std::vector<index_t> include = {0};
    auto m = g.find_matching(include);
    if(m)
    {
        std::cout << "Found matching:\n";
        for(auto p : m.value())
        {
            std::cout << p.first << " -- " << p.second << "\n";
        }
    }
    else
    {
        std::cout << "No matching found\n";
    }
}

void test_hc()
{
    HC hc {{{1,2}, {2,3}, {1,2,3}}};
    std::cout << hc.to_string() << "\n";
    point a {1,2,3};
    std::cout << hc.contains(a) << "\n";
    search_space ss1 = hc.remove_point(a);
    std::cout << ss1.to_string() << "\n";
    
    std::map<index_t, integer_set> fixed_axes = {{0,{1}},{2,{1,2}}};
    slice sl {fixed_axes};
    search_space ss2 = hc.remove_slice(sl);
    std::cout << ss2.to_string() << "\n";
}

void test_remove()
{
    HC hc {integer_set{0}, integer_set{2}};
    std::cout << hc.to_string() << "\n";
    std::map<index_t, integer_set> fixed_axes = {{0,{0}}};
    slice sl {fixed_axes};
    search_space ss2 = hc.remove_slice(sl);
    std::cout << ss2.to_string() << "\n";
}

void test_hardcoded_graph()
{
    graph g(10);
    g.add_edge(2, 1);
    g.add_edge(3, 2);
    g.add_edge(4, 2);
    g.add_edge(5, 0);
    g.add_edge(5, 1);
    g.add_edge(5, 2);
    g.add_edge(5, 3);
    g.add_edge(5, 4);
    g.add_edge(6, 3);
    g.add_edge(7, 0);
    g.add_edge(7, 4);
    g.add_edge(8, 0);
    g.add_edge(8, 3);
    g.add_edge(8, 4);
    std::vector<index_t> must_include = {0, 1, 2, 5, 6, 7, 8};
    auto res = g.find_matching(must_include);
    std::cout << g.to_string();
    if(res)
    {
        print_range("Got matching: ", *res);
    }
    else
    {
        std::cout << "No matching found" << std::endl;
    }
}

int main()
{
    test_graph();
    //test_discrete_graph();
    test_hc();
    //test_remove();
//    HC h2 = {{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}}};
//    point b = {20};
//    std::cout << h2.contains(b) << "\n";
//    search_space ss3 = h2.remove_point(b);
//    std::cout << ss3.to_string() << "\n";
    test_hardcoded_graph();
    std::cerr << "---= test_hcutils.cpp: all passed =---" << std::endl;
    return 0;
}
