#undef NDEBUG
#include <iostream>
#include <cassert>
#include "hypercuboid.h"

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
    local.remove_slice_backwards(problem, false);
    assert(local.size() == 3);
    assert(local.contains(point{0}));
    assert(local.contains(point{2}));
    assert(local.contains(point{3}));

    // With the fixed weight of 2, three hits provide enough evidence to cross
    // one miss and reach the intersecting front HC.
    search_space dense_enough{
        HC{{0, 4}},
        HC{{5}},
        HC{{0, 6}},
        HC{{0, 3}},
        HC{{0, 2}}
    };
    dense_enough.remove_slice_backwards(problem, false);
    assert(!dense_enough.contains(point{0}));
    assert(dense_enough.contains(point{2}));
    assert(dense_enough.contains(point{3}));
    assert(dense_enough.contains(point{4}));
    assert(dense_enough.contains(point{5}));
    assert(dense_enough.contains(point{6}));

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
        broad_problem, false);
    assert(rejected_as_too_fragmented.size() == 1);
    assert(rejected_as_too_fragmented.contains(point{0, 0}));

    // The originating HC must be cut even when the same operation would be
    // rejected for a secondary HC, or exploration could repeat forever.
    search_space partial{
        HC{{0, 1}, {0, 1}}
    };
    partial.remove_slice_backwards(broad_problem, true);
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
    dense.remove_slice_backwards(problem, false);
    assert(dense.size() == 2);
    assert(!dense.contains(point{0}));
    assert(dense.contains(point{1}));
    assert(dense.contains(point{2}));

}

int main()
{
    test_hc();
    test_backward_slice_removal();
    //test_remove();
//    HC h2 = {{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}}};
//    point b = {20};
//    std::cout << h2.contains(b) << "\n";
//    search_space ss3 = h2.remove_point(b);
//    std::cout << ss3.to_string() << "\n";
    std::cerr << "---= test_hcutils.cpp: all passed =---" << std::endl;
    return 0;
}
