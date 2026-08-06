#include <iostream>
#include <cassert>
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

void test_backward_slice_removal()
{
    const slice problem{{{0, {0}}}};

    // The back HC intersects, the preceding HC does not, so the dynamic
    // density rule stops before reaching the intersecting front HC.
    search_space local{
        HC{{0, 1}},
        HC{{2}},
        HC{{0, 3}}
    };
    local.remove_slice_backwards(problem);
    assert(local.size() == 3);
    assert(local.contains(point{0}));
    assert(local.contains(point{2}));
    assert(local.contains(point{3}));

    // The scan density is configurable. Two hits provide enough evidence for
    // weight 1 to cross one miss and reach the intersecting front HC.
    search_space permissive{
        HC{{0, 4}},
        HC{{5}},
        HC{{0, 3}},
        HC{{0, 2}}
    };
    permissive.remove_slice_backwards(problem, 1);
    assert(!permissive.contains(point{0}));
    assert(permissive.contains(point{2}));
    assert(permissive.contains(point{3}));
    assert(permissive.contains(point{4}));
    assert(permissive.contains(point{5}));

    // Weight zero disables early stopping and therefore crosses any number of
    // misses to remove the slice from every intersecting HC.
    search_space exhaustive{
        HC{{0, 6}},
        HC{{7}},
        HC{{8}},
        HC{{0, 9}}
    };
    exhaustive.remove_slice_backwards(problem, 0);
    assert(!exhaustive.contains(point{0}));
    assert(exhaustive.contains(point{6}));
    assert(exhaustive.contains(point{7}));
    assert(exhaustive.contains(point{8}));
    assert(exhaustive.contains(point{9}));

    // A slice may only partially intersect an HC on several axes. Backward
    // removal must not introduce slice coordinates that were absent from the
    // original HC.
    search_space rejected_as_too_fragmented{
        HC{{0, 1}, {0, 1}}
    };
    const slice broad_problem{{
        {0, {0, 9}},
        {1, {0, 9}}
    }};
    assert(!rejected_as_too_fragmented.back()
                .is_slice_good(broad_problem));
    assert(rejected_as_too_fragmented.back()
               .is_slice_good(broad_problem, 2));
    rejected_as_too_fragmented.remove_slice_backwards(
        broad_problem, 0);
    assert(rejected_as_too_fragmented.size() == 1);
    assert(rejected_as_too_fragmented.contains(point{0, 0}));

    // Raising max_codim to two accepts the same secondary removal.
    search_space accepted_with_codim_two{
        HC{{0, 1}, {0, 1}}
    };
    accepted_with_codim_two.remove_slice_backwards(
        broad_problem, 0, false, 2);
    assert(accepted_with_codim_two.size() == 2);
    assert(!accepted_with_codim_two.contains(point{0, 0}));
    assert(accepted_with_codim_two.contains(point{0, 1}));
    assert(accepted_with_codim_two.contains(point{1, 0}));
    assert(accepted_with_codim_two.contains(point{1, 1}));

    // The originating HC must be cut even when the same operation would be
    // rejected for a secondary HC, or exploration could repeat forever.
    search_space partial{
        HC{{0, 1}, {0, 1}}
    };
    partial.remove_slice_backwards(broad_problem, 0, true);
    assert(!partial.contains(point{0, 0}));
    assert(partial.contains(point{0, 1}));
    assert(partial.contains(point{1, 0}));
    assert(partial.contains(point{1, 1}));
    assert(!partial.contains(point{9, 1}));
    assert(!partial.contains(point{1, 9}));

    // With no intervening miss, all intersecting HCs are pruned.
    search_space dense{
        HC{{0, 1}},
        HC{{0, 2}}
    };
    dense.remove_slice_backwards(problem);
    assert(dense.size() == 2);
    assert(!dense.contains(point{0}));
    assert(dense.contains(point{1}));
    assert(dense.contains(point{2}));

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
    test_backward_slice_removal();
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
