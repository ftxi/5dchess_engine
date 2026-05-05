// created by ftxi on 2025/9/21
// library for multi-dimensional linear geometry, component of hypercuboid algorithm

#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <vector>
#include <list>
#include <string>
#include <map>
#include "integer_set.h"

// a point in the multi-dimensional space
using point = std::vector<index_t>;

class search_space;
struct slice;

class HC
{
    // a hypercuboid is represented as a list axes
    // it looks like: {axis_0, axis_1, ...}
    // where each axis_i is a sorted set of integers representing the allowed values on that axis
    // in actual computation, we only store the indices
    std::vector<integer_set> axes;
public:
    HC(std::initializer_list<integer_set> init_axes);
    explicit HC(std::vector<integer_set> &&init_axes);
    const integer_set &operator[](size_t i) const;
    integer_set &operator[](size_t i);
    bool contains(point p) const;
    bool empty() const;
    size_t volume() const;
    /* remove_slice and remove_point only work when it actually contains
    the stuff to be removed; otherwise, expect duplicate hcs */
    search_space remove_slice(const slice &s) const;
    search_space remove_point(const point &p) const;
    /* general purpose methods that includes a safety check */
    search_space remove_slice_carefully(const slice &s) const;
    search_space remove_point_carefully(const point &p) const;
    /* split the hypercuboid along the nth axis at the ith value 
    returns {part with ith value, part without ith value}
    */
    std::pair<HC, HC> split(index_t n, index_t i) const;

    std::string to_string(bool verbose=true) const;
};

struct slice
{
    std::map<index_t, integer_set> fixed_axes; // map from axis index to all options of the fixed value
    // other axes are free, i.e. all included in the slice represented
    bool contains(const point &p) const;
    std::string to_string() const;
};

class search_space
{
    // the search space is a union of hypercuboids
    // represented as a list of hypercuboids
    std::list<HC> hcs;
public:
    search_space() = default;
    search_space(std::initializer_list<HC> init_hcs);
    bool empty() const;
    size_t volume() const;
    bool contains(point p) const;
    void concat(search_space &&other);
    void prune_empty();
    std::string to_string() const;

    void push_back(HC hc);
    void push_front(HC hc);
    HC &back();
    const HC &back() const;
    void pop_back();

    std::list<HC>::iterator begin();
    std::list<HC>::iterator end();
    std::list<HC>::const_iterator begin() const;
    std::list<HC>::const_iterator end() const;
};

#endif /* GEOMETRY_H */
