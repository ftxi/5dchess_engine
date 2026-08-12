#include <cassert>
#include <random>
#include <vector>
#include "ordering.h"

template<HC_ordering Order>
std::vector<index_t> collect(
    const Order &order,
    index_t axis,
    const integer_set &allowed)
{
    std::vector<index_t> result;
    order.for_each(axis, allowed, [&](index_t i) {
        result.push_back(i);
    });
    return result;
}

int main()
{
    HC universe{integer_set{0, 1, 2, 3}, integer_set{4, 8}};

    natural_HC_ordering natural;
    assert((collect(natural, 0, universe[0]) ==
            std::vector<index_t>{0, 1, 2, 3}));

    std::mt19937 rng1(12345);
    std::mt19937 rng2(12345);
    random_HC_ordering random1(universe, rng1);
    random_HC_ordering random2(universe, rng2);

    const auto full1 = collect(random1, 0, universe[0]);
    const auto full2 = collect(random2, 0, universe[0]);
    assert(full1 == full2);

    integer_set subset{0, 2, 3};
    const auto filtered = collect(random1, 0, subset);
    std::vector<index_t> expected;
    for(index_t i : full1)
    {
        if(subset.contains(i))
        {
            expected.push_back(i);
        }
    }
    assert(filtered == expected);
}
