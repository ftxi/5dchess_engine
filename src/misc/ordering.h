#ifndef ORDERING_H
#define ORDERING_H

#include <algorithm>
#include <concepts>
#include <iterator>
#include <random>
#include <vector>
#include <utility>
#include "integer_set.h"
#include "geometry.h"

/*
Given a hypercuboid (as universe), assign each axis a consistent ordering

`order(n, s)` returns a forward range over `s` in the order assigned to
axis `n`. Every coordinate occurs exactly once. Repeated traversal preserves
the order and does not draw fresh randomness. The ordering and set must remain
alive and unchanged during traversal; the returned range does not allocate.

In principle `s` should be a subset of axis `n` of the hypercuboid, but there
is no enforcement of this. Use at your own risk.

The code is written as static polymorphism.
*/

template<class R>
concept HCCoordinateRange = requires(R &r)
{
    { r.begin() } -> std::forward_iterator;
    { r.end() } -> std::sentinel_for<decltype(r.begin())>;
    requires std::same_as<std::iter_value_t<decltype(r.begin())>, index_t>;
};

template<class T>
concept HCOrdering = requires(const T &order, index_t n, const integer_set &s)
{
    { order(n, s) } -> HCCoordinateRange;
};

namespace detail {

class natural_coordinate_range
{
    const integer_set *allowed;
public:
    explicit natural_coordinate_range(const integer_set &s) : allowed{&s} {}
    auto begin() const noexcept { return allowed->begin(); }
    auto end() const noexcept { return allowed->end(); }
};

class ordered_coordinate_range
{
    const std::vector<index_t> *ordering;
    const integer_set *allowed;
public:
    class iterator
    {
        using base_iterator = std::vector<index_t>::const_iterator;
        base_iterator current{};
        base_iterator finish{};
        const integer_set *allowed = nullptr;

        void skip_excluded() noexcept
        {
            while(current != finish && !allowed->contains(*current)) ++current;
        }
    public:
        using value_type = index_t;
        using difference_type = std::ptrdiff_t;
        using reference = index_t;
        using pointer = void;
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;

        iterator() = default;
        iterator(base_iterator current, base_iterator finish, const integer_set &s)
            : current{current}, finish{finish}, allowed{&s}
        {
            skip_excluded();
        }
        index_t operator*() const noexcept { return *current; }
        iterator &operator++() noexcept
        {
            ++current;
            skip_excluded();
            return *this;
        }
        iterator operator++(int) noexcept
        {
            iterator previous = *this;
            ++*this;
            return previous;
        }
        bool operator==(const iterator &other) const noexcept
        {
            return current == other.current;
        }
    };

    ordered_coordinate_range(const std::vector<index_t> &ordering, const integer_set &s)
        : ordering{&ordering}, allowed{&s} {}
    iterator begin() const noexcept { return {ordering->begin(), ordering->end(), *allowed}; }
    iterator end() const noexcept { return {ordering->end(), ordering->end(), *allowed}; }
};

} /* namespace detail */

/*
There are four orderings implemented in this file:
1. `natural_HC_ordering`: the natural ordering of the hypercuboid axes
2. `random_HC_ordering`: a random ordering of the hypercuboid axes
3. `scored_HC_ordering`: a custom ordering of the hypercuboid axes based on customized scores
where larger scores are selected first
4. `weighted_HC_ordering`: a custom ordering of the hypercuboid axes based on customized
probablistic weights where larger weights are more likely to be selected first
*/

/*==========Predefined Orderings==========*/

class natural_HC_ordering
{
public:
    natural_HC_ordering() = default;
    detail::natural_coordinate_range operator()(index_t, const integer_set &s) const
    {
        return detail::natural_coordinate_range{s};
    }
};

static_assert(HCOrdering<natural_HC_ordering>);

namespace detail {

// CRTP base class for shared methods of precomputed orderings.
template<class Derived>
struct precomputed_HC_ordering
{
    std::vector<std::vector<index_t>> orderings;
    ordered_coordinate_range operator()(index_t n, const integer_set &s) const
    {
        return {orderings[n], s};
    }
    static std::mt19937 &default_rng()
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        return rng;
    }
};

} /* namespace detail */

class random_HC_ordering : private detail::precomputed_HC_ordering<random_HC_ordering>
{
    using base = detail::precomputed_HC_ordering<random_HC_ordering>;
public:
    explicit random_HC_ordering(const HC &hc)
        : random_HC_ordering(hc, base::default_rng())
    {}

    random_HC_ordering(const HC &hc, std::mt19937 &rng);
    using base::operator();

    // Shuffle candidates independently of the precomputed per-axis order.
    template<class Range>
    static void shuffle(Range &values, std::mt19937 *rng = nullptr)
    {
        std::shuffle(values.begin(), values.end(),
                     rng ? *rng : base::default_rng());
    }
};

static_assert(HCOrdering<random_HC_ordering>);

/*==========Customizable Orderings==========*/

class scored_HC_ordering : private detail::precomputed_HC_ordering<scored_HC_ordering>
{
    using base = detail::precomputed_HC_ordering<scored_HC_ordering>;
public:
    scored_HC_ordering(const HC &hc, const std::vector<std::vector<float>> &scores)
        : scored_HC_ordering(hc, scores, base::default_rng())
    {}
    scored_HC_ordering(const HC &hc, const std::vector<std::vector<float>> &scores, std::mt19937 &rng);
    std::size_t rank(index_t n, index_t i) const;
    using base::operator();
};

static_assert(HCOrdering<scored_HC_ordering>);

class weighted_HC_ordering : private detail::precomputed_HC_ordering<weighted_HC_ordering>
{
    using base = detail::precomputed_HC_ordering<weighted_HC_ordering>;
public:
    explicit weighted_HC_ordering(const HC &hc, std::vector<std::vector<float>> weights)
        : weighted_HC_ordering(hc, std::move(weights), base::default_rng())
    {}

    weighted_HC_ordering(const HC &hc, std::vector<std::vector<float>> weights, std::mt19937 &rng);
    using base::operator();
};

static_assert(HCOrdering<weighted_HC_ordering>);

#endif /* ORDERING_H */
