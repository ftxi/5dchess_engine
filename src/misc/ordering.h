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
concept HC_ordering =
    requires(const T &order, index_t n, const integer_set& s)
{
    order.for_each(n, s, [](index_t) {});
};

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

static_assert(HC_ordering<natural_HC_ordering>);

class random_HC_ordering
{
    std::vector<std::vector<index_t>> orderings;

    static std::mt19937 &default_rng()
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        return rng;
    }
public:
    explicit random_HC_ordering(const HC &hc)
        : random_HC_ordering(hc, default_rng())
    {}

    random_HC_ordering(const HC &hc, std::mt19937 &rng)
    {
        orderings.resize(hc.dimension());
        for(index_t n = 0; n < hc.dimension(); n++)
        {
            orderings[n].assign(hc[n].begin(), hc[n].end());
            std::shuffle(orderings[n].begin(), orderings[n].end(), rng);
        }
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

static_assert(HC_ordering<random_HC_ordering>);

#endif /* ORDERING_H */
