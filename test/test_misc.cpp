#undef NDEBUG
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>
#include "integer_set.h"
#include "utils.h"
#include "generator.h"
#include <cassert>
#include "ordering.h"
#include "turn.h"

namespace integer_set_tests
{
static_assert(std::is_same_v<
    decltype(std::declval<integer_set &>() |= std::declval<const integer_set &>()),
    integer_set &>);
static_assert(std::is_same_v<
    decltype(std::declval<integer_set &>() &= std::declval<const integer_set &>()),
    integer_set &>);
static_assert(noexcept(std::declval<const integer_set &>().is_subset_of(
    std::declval<const integer_set &>())));

template <typename T>
std::vector<uint32_t> snapshot(const T &values)
{
    return std::vector<uint32_t>(values.begin(), values.end());
}

int run()
{
    // Compare subset queries against the standard sorted-range algorithm,
    // including unequal storage lengths and retained zero blocks.
    std::vector<integer_set> subset_cases{
        {}, {0}, {1, 63}, {64}, {1, 64, 128}, {65, 4096}, {1, 63, 64, 65, 128, 4096}
    };
    integer_set retained_blocks{4096};
    retained_blocks.erase(4096);
    subset_cases.push_back(retained_blocks);
    retained_blocks.insert(1);
    subset_cases.push_back(retained_blocks);
    std::mt19937 rng(42);
    for(int sample = 0; sample < 40; sample++)
    {
        integer_set values;
        for(int i = 0; i < 20; i++)
        {
            values.insert(rng() % 512);
        }
        subset_cases.push_back(std::move(values));
    }
    for(const integer_set &a : subset_cases)
    {
        for(const integer_set &b : subset_cases)
        {
            const auto a_values = snapshot(a);
            const auto b_values = snapshot(b);
            const bool expected = std::includes(
                b_values.begin(), b_values.end(), a_values.begin(), a_values.end());
            if(a.is_subset_of(b) != expected)
            {
                std::cerr << "Test failed: subset query disagrees with reference\n";
                return 1;
            }
            std::vector<uint32_t> common, visited;
            std::set_intersection(a_values.begin(), a_values.end(),
                b_values.begin(), b_values.end(), std::back_inserter(common));
            a.for_each_intersection(b, [&](index_t i) { visited.push_back(i); });
            assert(visited == common);
        }
    }

    integer_set s{1,2,3,5,64,65,98};
    std::set<uint32_t> _s{1,2,3,5,64,65,98};
    print_range("s: ", s);
    for(uint32_t i = 0; i < 16; i++)
    {
        std::cout << "Does s contain " << i << "? ";
        std::cout << s.contains(i) << "\n";
    }
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed: snapshot does not match reference\n";
        return 1;
    }
    s.erase(3);
    _s.erase(3);
    print_range("s after erase 3: ", s);
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed after erase\n";
        print_range("reference: ", _s);
        return 1;
    }
    s.insert(7);
    _s.insert(7);
    print_range("s after insert 7: ", s);
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed after insert\n";
        print_range("reference: ", _s);
        return 1;
    }
    integer_set t{5,6,7,64,128};
    std::set<uint32_t> _t{5,6,7,64,128};
    s.minus(t);
    print_range("s after minus t: ", s);
    for(uint32_t value : _t)
    {
        _s.erase(value);
    }
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed after set minus\n";
        print_range("reference: ", _s);
        return 1;
    }
    integer_set u = s.transform([](uint32_t x) { return x * x; });
    std::set<uint32_t> _u;
    for(uint32_t value : snapshot(s))
    {
        _u.insert(value * value);
    }
    print_range("u = s transform by square = ", u);
    if(snapshot(u) != snapshot(_u))
    {
        std::cerr << "Test failed after transform\n";
        print_range("reference: ", _u);
        return 1;
    }
    u.erase_if([](uint32_t x) { return x % 2 == 0; });
    print_range("u after erase_if even: ", u);
    std::erase_if(_u, [](uint32_t x) { return x % 2 == 0; });
    if(snapshot(u) != snapshot(_u))
    {
        std::cerr << "Test failed after erase_if\n";
        print_range("reference: ", _u);
        return 1;
    }
    u |= integer_set{1, 2, 3, 200, 300};
    _u.insert({1, 2, 3, 200, 300});
    print_range("u after union with {1,2,3,200,300}: ", u);
    if(snapshot(u) != snapshot(_u))    {
        std::cerr << "Test failed after union\n";
        print_range("reference: ", _u);
        return 1;
    }
    u &= integer_set{2, 3, 4, 200};
    std::erase_if(_u, [](uint32_t x) { return !(x == 2 || x == 3 || x == 4 || x == 200); });
    print_range("u after intersection with {2,3,4,200}: ", u);
    if(snapshot(u) != snapshot(_u))
    {   
        std::cerr << "Test failed after intersection\n";
        print_range("reference: ", _u);
        return 1;
    }

    // Exercise removal from dynamically stored higher blocks through every
    // removing operation, including a set whose retained blocks are empty.
    integer_set removed_by_erase{1, 64, 4096};
    removed_by_erase.erase(4096);
    removed_by_erase.erase(64);
    if(snapshot(removed_by_erase) != std::vector<uint32_t>{1})
    {
        std::cerr << "Test failed after high-block erase\n";
        return 1;
    }

    integer_set removed_by_minus{2, 65, 4097};
    removed_by_minus.minus(integer_set{65, 4097});
    if(snapshot(removed_by_minus) != std::vector<uint32_t>{2})
    {
        std::cerr << "Test failed after high-block minus\n";
        return 1;
    }

    integer_set removed_by_intersection{3, 66, 4098};
    removed_by_intersection &= integer_set{3};
    if(snapshot(removed_by_intersection) != std::vector<uint32_t>{3})
    {
        std::cerr << "Test failed after high-block intersection\n";
        return 1;
    }

    integer_set removed_by_predicate{4, 67, 4099};
    removed_by_predicate.erase_if([](uint32_t value) { return value >= 64; });
    if(snapshot(removed_by_predicate) != std::vector<uint32_t>{4})
    {
        std::cerr << "Test failed after high-block erase_if\n";
        return 1;
    }
    removed_by_predicate.erase(4);
    if(!removed_by_predicate.empty() || removed_by_predicate.begin() != removed_by_predicate.end())
    {
        std::cerr << "Test failed after retained blocks became empty\n";
        return 1;
    }

    std::cerr << "---= integer_set.cpp: all passed =---" << std::endl;
    return 0;
}
} /* namespace integer_set_tests */

namespace generator_tests
{
template<std::integral T>
generator<T> range(T first, const T last)
{
    while (first < last)
    {
        std::cerr << "generating " << first << " ";
        if(first == 80)
            co_return;
        co_yield first++;
    }
}

void test_range()
{
    auto g = range(65,99);
    std::cerr << "phase 1: ";
    if(auto k = g.first())
    {
        std::cerr << "got " << k.value() << "\n";
    }
    else
    {
        std::cerr << "got nothing\n";
    }
    std::cerr << "phase 2: ";
    if(auto k = g.first())
    {
        std::cerr << "got " << k.value() << "\n";
    }
    else
    {
        std::cerr << "got nothing\n";
    }
    std::cerr << "phase 3: ";
    for(auto l : g)
    {
        std::cerr << "then got " << l << "\n";
    }
    std::cerr << "test_range finished\n";
}

int run()
{
    test_range();
    std::cerr << "---= test_generator.cpp: all passed =---" << std::endl;
    return 0;
}
} /* namespace generator_tests */

namespace ordering_tests
{
static_assert(std::forward_iterator<integer_set::iterator>);
static_assert(std::forward_iterator<integer_set::const_iterator>);
static_assert(HCCoordinateRange<std::vector<index_t>>);
static_assert(!HCCoordinateRange<std::vector<float>>);

template<HCOrdering Order>
std::vector<index_t> collect(
    const Order &order,
    index_t axis,
    const integer_set &allowed)
{
    std::vector<index_t> result;
    auto &&coordinates = order(axis, allowed);
    auto first = coordinates.begin();
    auto copy = first;
    if(first != coordinates.end())
    {
        const index_t value = *copy;
        auto previous = first++;
        assert(*previous == value && *copy == value);
        assert(copy == coordinates.begin());
    }
    for(index_t i : coordinates) result.push_back(i);
    std::vector<index_t> repeated;
    for(index_t i : coordinates) repeated.push_back(i);
    assert(result == repeated);
    const integer_set empty;
    auto &&none = order(axis, empty);
    assert(none.begin() == none.end());
    using Iterator = decltype(first);
    assert(Iterator{} == Iterator{});
    return result;
}

void test_natural_ordering()
{
    HC universe{integer_set{0, 1, 2, 3}, integer_set{4, 8}};

    natural_HC_ordering natural;
    auto expected = std::vector<index_t>{0, 1, 2, 3};
    assert((collect(natural, 0, universe[0]) == expected));
}

void test_sparse_coordinate_ranges()
{
    HC universe{integer_set{0, 1, 63, 64, 127, 128, 511}};
    const integer_set allowed{0, 64, 128, 511};
    natural_HC_ordering natural;
    assert((collect(natural, 0, allowed) == std::vector<index_t>{0, 64, 128, 511}));
    std::vector<std::vector<float>> scores(1, std::vector<float>(512));
    for(index_t i : universe[0]) scores[0][i] = static_cast<float>(i + 1);
    std::mt19937 rng(71);
    scored_HC_ordering scored(universe, scores, rng);
    assert((collect(scored, 0, allowed) == std::vector<index_t>{511, 128, 64, 0}));
    random_HC_ordering random(universe, rng);
    weighted_HC_ordering weighted(universe, {{1, 2, 3, 4, 5, 6, 7}}, rng);
    const auto check_subset = [&](const auto &order) {
        std::vector<index_t> expected;
        for(index_t i : collect(order, 0, universe[0]))
            if(allowed.contains(i)) expected.push_back(i);
        assert(collect(order, 0, allowed) == expected);
    };
    check_subset(random);
    check_subset(weighted);
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

int run()
{
    test_natural_ordering();
    test_sparse_coordinate_ranges();
    test_random_ordering();
    test_scored_ordering();
    test_weighted_ordering();
    return 0;
}
} /* namespace ordering_tests */

namespace turn_tests
{
static_assert(sizeof(turn) == sizeof(int));
static_assert(std::is_trivially_copyable_v<turn>);
static_assert(turn{} == turn(0, false));
static_assert(turn(0, false) < turn(0, true));
static_assert(turn(0, true) < turn(1, false));
static_assert(turn(1, false).next_turn() == turn(1, true));
static_assert(turn(1, true).next_turn() == turn(2, false));
static_assert(turn(1, false).previous_turn() == turn(0, true));
static_assert(turn(0, true).previous_turn() == turn{});
static_assert(turn::max.index() == std::numeric_limits<int>::max());
static_assert(turn::max.t() == std::numeric_limits<int>::max() / 2);
static_assert(turn::max.c());
static_assert(turn(turn::max.t(), true) == turn::max);
static_assert(turn::max.previous_turn().next_turn() == turn::max);

constexpr bool increment_and_decrement()
{
    turn at{0, true};
    if(at++ != turn(0, true) || at != turn(1, false)) return false;
    if(&(++at) != &at || at != turn(1, true)) return false;
    if(at-- != turn(1, true) || at != turn(1, false)) return false;
    return &(--at) == &at && at == turn(0, true);
}
static_assert(increment_and_decrement());

int run()
{
    for(int t = 0; t < 100; ++t)
    {
        for(bool c : {false, true})
        {
            const turn at{t, c};
            assert(turn::from_index(at.index()) == at);
            assert(at.get() == std::make_pair(t, c));
            assert(at.to_string() == std::to_string(t) + (c ? "b" : "w"));
        }
    }
    assert(turn::max.to_string() == std::to_string(turn::max.t()) + "b");
    return 0;
}
} /* namespace turn_tests */

int main()
{
    std::cerr << "Running turn tests\n";
    if(const int result = turn_tests::run(); result != 0)
        return result;
    std::cerr << "Running integer_set tests\n";
    if(const int result = integer_set_tests::run(); result != 0)
        return result;
    std::cerr << "Running generator tests\n";
    if(const int result = generator_tests::run(); result != 0)
        return result;
    std::cerr << "Running ordering tests\n";
    if(const int result = ordering_tests::run(); result != 0)
        return result;
    return 0;
}
