#ifndef ORDERING_H
#define ORDERING_H

#include <algorithm>
#include <concepts>
#include <random>
#include <vector>
#include "integer_set.h"
#include "geometry.h"

/*
Given a hypercuboid (as universe), assign each axis a consistent ordering

`for_each(n,s,f)` iterates over the elements of `s` in the order assigned to axis `n`, calling `f(i)` for each element `i`.

In principle `s` should be a subset of axis `n` of the hypercuboid, but there
is no enforcement of this. Use at your own risk.

The code is written as static polymorphism.
*/

template<typename T>
concept HCOrdering =
    requires(const T &order, index_t n, const integer_set& s)
{
    order.for_each(n, s, [](index_t) {});
};

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
    void for_each(index_t, const integer_set& s, auto &&f) const
    {
        for(index_t i: s)
        {
            f(i);
        }
    }
};

static_assert(HCOrdering<natural_HC_ordering>);

namespace detail {

// CRTP base class for shared methods of precomputed orderings.
template<class Derived>
struct precomputed_HC_ordering
{
    std::vector<std::vector<index_t>> orderings;
    static std::mt19937 &default_rng()
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        return rng;
    }
    void for_each(index_t n, const integer_set& s, auto &&f) const
    {
        for(index_t i: orderings[n])
        {
            if(s.contains(i))
            {
                f(i);
            }
        }
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
    using base::for_each;
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
    using base::for_each;
};

static_assert(HCOrdering<scored_HC_ordering>);

class weighted_HC_ordering : private detail::precomputed_HC_ordering<weighted_HC_ordering>
{
    using base = detail::precomputed_HC_ordering<weighted_HC_ordering>;
public:
    explicit weighted_HC_ordering(const HC &hc, std::vector<std::vector<float>> weights)
        : weighted_HC_ordering(hc, weights, base::default_rng())
    {}

    weighted_HC_ordering(const HC &hc, std::vector<std::vector<float>> weights, std::mt19937 &rng);
    using base::for_each;
};

static_assert(HCOrdering<weighted_HC_ordering>);

#endif /* ORDERING_H */
