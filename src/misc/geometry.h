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
class slice;

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
    bool contains(const point &p) const;
    bool intersects(const slice &s) const;
    bool empty() const;
    size_t volume() const;
    /* remove_slice and remove_point only work when it actually contains
    the stuff to be removed; otherwise, expect duplicate hcs */
    search_space remove_slice(const slice &s) const;
    search_space remove_point(const point &p) const;
    /* general purpose methods that includes a safety check */
    search_space remove_slice_carefully(const slice &s) const;
    search_space remove_point_carefully(const point &p) const;
    /* check if the slice has codimension less than or equal to max_codim */
    bool is_slice_good(const slice &s, index_t max_codim = 1) const;
    /* remove slice if the codimension of the slice in this hc is small */
    search_space remove_slice_if_good(const slice &s, index_t max_codim = 1) const;
    /* split the hypercuboid along the nth axis at the ith value 
    returns {part with ith value, part without ith value}
    */
    std::pair<HC, HC> split(index_t n, index_t i) const;
    size_t dimension() const { return axes.size(); }
    std::string to_string(bool verbose=true) const;
};

class slice
{
    // map from axis index to all options of the fixed value
    std::map<index_t, integer_set> fixed_axes;
    // other axes are free, i.e. all included in the slice represented
public:
    slice() = default;
    explicit slice(std::map<index_t, integer_set> init_fixed_axes);

    const std::map<index_t, integer_set> &get_fixed_axes() const;
    void fix_axis(index_t n, integer_set values);
    void free_axis(index_t n);

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
    size_t size() const { return hcs.size(); }

    void push_back(HC hc);
    void push_front(HC hc);
    HC &back();
    const HC &back() const;
    void pop_back();
    /*
     Starting at back(), remove the slice while intersections remain locally
     dense. This uses the HC_info::search() stopping rule:
         continue while disjoint_weight * disjoint_count < intersect_count.
     A disjoint_weight of zero scans the entire list.
     */
    void remove_slice_backwards(
        const slice &s,
        size_t disjoint_weight = 10,
        bool force_back_removal = false,
        index_t max_codim = 1);

    std::list<HC>::iterator begin();
    std::list<HC>::iterator end();
    std::list<HC>::const_iterator begin() const;
    std::list<HC>::const_iterator end() const;
};

#endif /* GEOMETRY_H */
