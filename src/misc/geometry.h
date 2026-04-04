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
using point = std::vector<uint32_t>;

struct search_space;
struct slice;

struct HC
{
    // a hypercuboid is represented as a list axes
    // it looks like: {axis_0, axis_1, ...}
    // where each axis_i is a sorted set of integers representing the allowed values on that axis
    // in actual computation, we only store the indices
    std::vector<integer_set> axes;
    const integer_set &operator[](size_t i) const;
    bool contains(point p) const;
    search_space remove_slice(const slice &s) const;
    search_space remove_point(const point &p) const;
    std::string to_string(bool verbose=true) const;
};

struct slice
{
    std::map<uint32_t, integer_set> fixed_axes; // map from axis index to all options of the fixed value
    // other axes are free, i.e. all included in the slice represented
    bool contains(const point &p) const;
    std::string to_string() const;
};

struct search_space
{
    // the search space is a union of hypercuboids
    // represented as a list of hypercuboids
    std::list<HC> hcs;
    bool contains(point p) const;
    void concat(search_space &&other);
    std::string to_string() const;
};

#endif /* GEOMETRY_H */
