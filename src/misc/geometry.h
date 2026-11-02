// created by ftxi on 2025/9/21
// library for multi-dimensional linear geometry, component of hypercuboid algorithm

#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <vector>
#include <list>
#include <string>
#include <map>
#include <stop_token>
#include "integer_set.h"
#ifdef HC_PROFILE_METRICS
#include "hc_profile_metrics.h"
#endif

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
#ifdef HC_PROFILE_METRICS
    hc_profile::hc_lifetime lifetime;
#endif
public:
    HC(std::initializer_list<integer_set> init_axes);
    explicit HC(std::vector<integer_set> &&init_axes) noexcept;
    const integer_set &operator[](size_t i) const & noexcept;
    integer_set &operator[](size_t i) & noexcept;
    const integer_set &operator[](size_t i) const && = delete;
    bool contains(const point &p) const noexcept;
    bool intersects(const slice &s) const noexcept;
    bool empty() const noexcept;
    size_t volume() const noexcept;
    /* remove_slice and remove_point only work when it actually contains
    the stuff to be removed; otherwise, expect duplicate hcs */
    search_space remove_slice(const slice &s) const &;
    // Consume an owned HC, reusing it for the final nonempty piece.
    search_space remove_slice(const slice &s) &&;
    search_space remove_point(const point &p) const &;
    // Reuse an owned HC for the final piece; output order is unchanged.
    search_space remove_point(const point &p) &&;
    /* general purpose methods that includes a safety check */
    search_space remove_slice_carefully(const slice &s) const &;
    search_space remove_slice_carefully(const slice &s) &&;
    search_space remove_point_carefully(const point &p) const;
    /* check if the slice has codimension less than or equal to max_codim */
    bool is_slice_good(const slice &s, index_t max_codim = 1) const noexcept;
    /* remove slice if the codimension of the slice in this hc is small */
    search_space remove_slice_if_good(const slice &s, index_t max_codim = 1) const &;
    search_space remove_slice_if_good(const slice &s, index_t max_codim = 1) &&;
    /* split the hypercuboid along the nth axis at the ith value 
    returns {part with ith value, part without ith value}
    */
    std::pair<HC, HC> split(index_t n, index_t i) const;
    size_t dimension() const noexcept { return axes.size(); }
    std::string to_string(bool verbose=true) const;
};

class slice
{
    // map from axis index to all options of the fixed value
    std::map<index_t, integer_set> fixed_axes;
    // other axes are free, i.e. all included in the slice represented
public:
    slice() = default;
    explicit slice(std::map<index_t, integer_set> init_fixed_axes) noexcept;

    const std::map<index_t, integer_set> &get_fixed_axes() const & noexcept;
    const std::map<index_t, integer_set> &get_fixed_axes() const && = delete;
    void fix_axis(index_t n, integer_set values);
    void free_axis(index_t n);

    bool contains(const point &p) const noexcept;
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
    bool empty() const noexcept;
    size_t volume() const noexcept;
    bool contains(const point &p) const noexcept;
    void concat(search_space &&other) noexcept;
    void prune_empty();
    std::string to_string() const;
    size_t size() const noexcept { return hcs.size(); }

    void push_back(HC hc);
    void push_front(HC hc);
    HC &back() & noexcept;
    const HC &back() const & noexcept;
    const HC &back() const && = delete;
    void pop_back() noexcept;
    /*
     Starting at back(), remove the slice while intersections remain locally
     dense. Replace each HC in place to preserve search order. Continue while
     disjoint_weight * disjoint_count < intersect_count.
     Limit secondary removals to max_codim; forced back removal ignores it.
     */
    void remove_slice_backwards(
        const slice &s,
        bool force_back_removal,
        size_t disjoint_weight = 2,
        index_t max_codim = 1
    );

    // Remove from every HC without a quality/density limit. Replace intersecting
    // HCs in place, preserving the order of all original regions and pieces.
    // Cancellation may leave a partially pruned space; callers must check stop.
    void remove_slice_globally(const slice &s, std::stop_token stop = {});

    std::list<HC>::iterator begin() & noexcept;
    std::list<HC>::iterator end() & noexcept;
    std::list<HC>::const_iterator begin() const & noexcept;
    std::list<HC>::const_iterator end() const & noexcept;
    std::list<HC>::const_iterator begin() const && = delete;
    std::list<HC>::const_iterator end() const && = delete;
};

#endif /* GEOMETRY_H */
