#undef NDEBUG
#include <cassert>
#include <random>
#include <vector>
#include "ordering.h"

template<HCOrdering Order>
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

void test_natural_ordering()
{
    HC universe{integer_set{0, 1, 2, 3}, integer_set{4, 8}};

    natural_HC_ordering natural;
    auto expected = std::vector<index_t>{0, 1, 2, 3};
    assert((collect(natural, 0, universe[0]) == expected));
}

void test_random_ordering()
{
    HC universe{integer_set{0, 1, 2, 3}, integer_set{4, 8}};

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

void test_scored_ordering()
{
    HC universe{
        integer_set{0, 1, 2, 3},
        integer_set{4, 8},
    };
    const std::vector<std::vector<float>> scores{
        {2.0f, 4.0f, 1.0f, 3.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         0.0f, 0.0f, 0.0f, 2.0f},
    };

    std::mt19937 rng(12345);
    scored_HC_ordering order(universe, scores, rng);

    assert((collect(order, 0, universe[0]) ==
            std::vector<index_t>{1, 3, 0, 2}));
    assert((collect(order, 1, universe[1]) ==
            std::vector<index_t>{8, 4}));

    // Equal scores are randomized reproducibly by a supplied RNG.
    const std::vector<std::vector<float>> tied_scores{
        {1.0f, 1.0f, 1.0f, 1.0f},
        scores[1],
    };
    std::mt19937 tied_rng1(67890);
    std::mt19937 tied_rng2(67890);
    scored_HC_ordering tied1(universe, tied_scores, tied_rng1);
    scored_HC_ordering tied2(universe, tied_scores, tied_rng2);
    assert(collect(tied1, 0, universe[0]) ==
           collect(tied2, 0, universe[0]));

    const integer_set subset{0, 2, 3};
    assert((collect(order, 0, subset) ==
            std::vector<index_t>{3, 0, 2}));
}

void test_weighted_ordering()
{
    HC universe{
        integer_set{0, 1, 2, 3},
        integer_set{1, 2, 3, 4, 5},
    };

    constexpr float tiny_weight = 1e-24f;
    static_assert(tiny_weight > 0.0f);
    for(unsigned int seed = 0; seed < 500; ++seed)
    {
        std::mt19937 rng(seed);
        weighted_HC_ordering order(
            universe,
            {
                {1.0f, tiny_weight, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            },
            rng);

        // Positive values, including a tiny positive weight, always precede
        // zero-weight values.
        const auto first_axis = collect(order, 0, universe[0]);
        assert(first_axis[0] < 2 && first_axis[1] < 2);
        assert(first_axis[2] >= 2 && first_axis[3] >= 2);

        // On the second axis, 3 is the only value with positive weight.
        const auto second_axis = collect(order, 1, universe[1]);
        assert(second_axis.front() == 3);
    }
}

int main()
{
    test_natural_ordering();
    test_random_ordering();
    test_scored_ordering();
    test_weighted_ordering();
}
