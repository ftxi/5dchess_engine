#include "geometry.h"
#include <cassert>
#include <iterator>
#include <sstream>
#include "utils.h"

HC::HC(std::initializer_list<integer_set> init_axes)
    : axes(init_axes)
{
}

HC::HC(std::vector<integer_set> &&init_axes) noexcept
    : axes(std::move(init_axes))
{
}

const integer_set &HC::operator[](size_t i) const & noexcept
{
    return axes[i];
}

integer_set &HC::operator[](size_t i) & noexcept
{
    return axes[i];
}

bool HC::contains(const point &loc) const noexcept
{
    assert(loc.size() == axes.size());
    for(index_t i = 0; i < static_cast<index_t>(axes.size()); i++)
    {
        if(!axes[i].contains(loc[i]))
            return false;
    }
    return true;
}

bool HC::intersects(const slice &s) const noexcept
{
    const auto &fixed_axes = s.get_fixed_axes();
    for(const auto &[i, fixed_coords] : fixed_axes)
    {
        if(!axes[i].intersects(fixed_coords))
        {
            return false;
        }
    }
    return true;
}

bool HC::empty() const noexcept
{
    for(const auto &axis : axes)
    {
        if(axis.empty())
            return true;
    }
    return false;
}

size_t HC::volume() const noexcept
{
    size_t result = 1;
    for(const auto &axis : axes)
    {
        result *= axis.size();
    }
    return result;
}

bool slice::contains(const point &p) const noexcept
{
    for(const auto &[n, coords] : fixed_axes)
    {
        if(!coords.contains(p[n]))
            return false;
    }
    return true;
}

slice::slice(std::map<index_t, integer_set> init_fixed_axes) noexcept
    : fixed_axes(std::move(init_fixed_axes))
{
}

const std::map<index_t, integer_set> &slice::get_fixed_axes() const & noexcept
{
    return fixed_axes;
}

void slice::fix_axis(index_t n, integer_set values)
{
    fixed_axes[n] = std::move(values);
}

void slice::free_axis(index_t n)
{
    fixed_axes.erase(n);
}

size_t search_space::volume() const noexcept
{
    size_t result = 0;
    for(const auto &hc : hcs)
    {
        result += hc.volume();
    }
    return result;
}

bool search_space::empty() const noexcept
{
    for(const auto &hc : hcs)
    {
        if(!hc.empty())
            return false;
    }
    return true;
}

bool search_space::contains(const point &loc) const noexcept
{
    for(const auto &hc : hcs)
    {
        if(hc.contains(loc))
            return true;
    }
    return false;
}

search_space::search_space(std::initializer_list<HC> init_hcs)
    : hcs(init_hcs)
{
}

void search_space::push_back(HC hc)
{
    hcs.push_back(std::move(hc));
}

void search_space::push_front(HC hc)
{
    hcs.push_front(std::move(hc));
}

HC &search_space::back() & noexcept
{
    return hcs.back();
}

const HC &search_space::back() const & noexcept
{
    return hcs.back();
}

void search_space::pop_back() noexcept
{
    hcs.pop_back();
}

void search_space::remove_slice_backwards(
    const slice &s,
    bool force_back_removal,
    size_t disjoint_weight,
    index_t max_codim
)
{
    if(hcs.empty())
    {
        return;
    }

    size_t intersect_count = 0;
    size_t disjoint_count = 0;
    auto current = std::prev(hcs.end());

    while(true)
    {
        const bool has_previous = current != hcs.begin();
        auto previous = current;
        if(has_previous)
        {
            --previous;
        }

        if(current->intersects(s))
        {
            auto after = std::next(current);
            search_space pieces = force_back_removal
                ? std::move(*current).remove_slice_carefully(s)
                : std::move(*current).remove_slice_if_good(s, max_codim);
            hcs.erase(current);
            hcs.splice(after, pieces.hcs);
            intersect_count++;
        }
        else
        {
            disjoint_count++;
        }

        if(!has_previous
           || disjoint_count * disjoint_weight >= intersect_count)
        {
            break;
        }
        force_back_removal = false;
        current = previous;
    }
}

void search_space::remove_slice_globally(const slice &s, std::stop_token stop)
{
    for(auto current = hcs.begin(); current != hcs.end();)
    {
        if(stop.stop_requested()) return;
        if(!current->intersects(s))
        {
            ++current;
            continue;
        }
        auto pieces = std::move(*current).remove_slice_carefully(s);
        current = hcs.erase(current);
        hcs.splice(current, pieces.hcs);
    }
}

std::list<HC>::iterator search_space::begin() & noexcept
{
    return hcs.begin();
}

std::list<HC>::iterator search_space::end() & noexcept
{
    return hcs.end();
}

std::list<HC>::const_iterator search_space::begin() const & noexcept
{
    return hcs.begin();
}

std::list<HC>::const_iterator search_space::end() const & noexcept
{
    return hcs.end();
}

void search_space::concat(search_space &&other) noexcept
{
    hcs.splice(hcs.end(), other.hcs);
}

void search_space::prune_empty()
{
    hcs.remove_if([](const HC &hc) {
        return hc.empty();
    });
}

search_space HC::remove_slice(const slice &s) const &
{
    search_space result;
    HC remaining = *this;
    for(const auto &[i, fixed_coords] : s.get_fixed_axes())
    {
        HC x = remaining;
        x.axes[i].minus(fixed_coords);
        remaining.axes[i] = fixed_coords;
        if(!x.axes[i].empty()) // do not include empty hc
        {
            result.push_back(std::move(x));
        }
    }
    return result;
}

namespace
{
// The caller has already checked intersection when needed. Axes are distinct,
// so the last nonempty output can be found before changing any prefix domain.
template<bool Careful>
search_space remove_slice_owned(HC &&remaining, const slice &s)
{
    const auto &fixed = s.get_fixed_axes();
    auto after_last = fixed.end();
    while(after_last != fixed.begin())
    {
        const auto last = std::prev(after_last);
        if(!remaining[last->first].is_subset_of(last->second)) break;
        after_last = last;
    }
    if(after_last == fixed.begin()) return {}; // The slice covers this HC.
    const auto last = std::prev(after_last);

    search_space result;
    for(auto current = fixed.begin(); current != last; ++current)
    {
        const auto &[axis, values] = *current;
        if(!remaining[axis].is_subset_of(values))
        {
            HC outside = remaining;
            outside[axis].minus(values);
            result.push_back(std::move(outside));
        }
        if constexpr(Careful) remaining[axis] &= values;
        else remaining[axis] = values;
    }
    remaining[last->first].minus(last->second);
    result.push_back(std::move(remaining));
    return result;
}

std::size_t point_split_end(const HC &hc, const point &p) noexcept
{
    std::size_t end = p.size();
    while(end > 0 && hc[end - 1].size() == 1) --end;
    return end;
}

search_space remove_point_impl(HC remaining, const point &p, std::size_t end)
{
    search_space result;
    for(std::size_t i = 0; i + 1 < end; ++i)
    {
        // Singleton axes already fix the prefix to p and have no output piece.
        if(remaining[i].size() == 1) continue;
        HC x = remaining;
        x[i].erase(p[i]);
        remaining[i] = integer_set{p[i]};
        result.push_back(std::move(x));
    }
    // No later piece needs the remaining prefix, so reuse its HC directly.
    remaining[end - 1].erase(p[end - 1]);
    result.push_back(std::move(remaining));
    return result;
}
} /* anonymous namespace */

search_space HC::remove_slice(const slice &s) &&
{
    return remove_slice_owned<false>(std::move(*this), s);
}

search_space HC::remove_point(const point &p) const &
{
    const auto end = point_split_end(*this, p);
    if(end == 0) return {};
    return remove_point_impl(*this, p, end);
}

search_space HC::remove_point(const point &p) &&
{
    const auto end = point_split_end(*this, p);
    if(end == 0) return {};
    return remove_point_impl(std::move(*this), p, end);
}

search_space HC::remove_slice_carefully(const slice &s) const &
{
    const auto &fixed_axes = s.get_fixed_axes();
    for(const auto &[i, fixed_coords] : fixed_axes)
    {
        if(!axes[i].intersects(fixed_coords))
        {
            return search_space({{*this}});
        }
    }

    search_space result;
    HC remaining = *this;
    for(const auto &[i, fixed_coords] : fixed_axes)
    {
        integer_set intersected_coords = fixed_coords;
        intersected_coords &= remaining.axes[i];
        assert(!intersected_coords.empty());

        HC x = remaining;
        x.axes[i].minus(intersected_coords);
        remaining.axes[i] = std::move(intersected_coords);
        if(!x.axes[i].empty()) // do not include empty hc
        {
            result.push_back(std::move(x));
        }
    }
    return result;
}

search_space HC::remove_slice_carefully(const slice &s) &&
{
    if(!intersects(s))
    {
        search_space result;
        result.push_back(std::move(*this));
        return result;
    }
    return remove_slice_owned<true>(std::move(*this), s);
}

search_space HC::remove_point_carefully(const point &p) const
{
    if(!contains(p))
    {
        return search_space({{*this}});
    }
    else
    {
        return remove_point(p);
    }
}

bool HC::is_slice_good(const slice &s, index_t max_codim) const noexcept
{
    const auto &fixed_axes = s.get_fixed_axes();
    index_t count = 0;
    for(const auto &[i, fixed_coords] : fixed_axes)
    {
        if(!axes[i].intersects(fixed_coords))
        {
            return false;
        }

        if(!axes[i].is_subset_of(fixed_coords))
        {
            if(count >= max_codim)
            {
                return false;
            }
            count++;
        }
    }
    return true;
}

search_space HC::remove_slice_if_good(const slice &s, index_t max_codim) const &
{
    if(!is_slice_good(s, max_codim))
    {
        return search_space({{*this}});
    }

    search_space result;
    HC remaining = *this;
    for(const auto &[i, fixed_coords] : s.get_fixed_axes())
    {
        integer_set intersected_coords = fixed_coords;
        intersected_coords &= remaining.axes[i];
        assert(!intersected_coords.empty());

        HC x = remaining;
        x.axes[i].minus(intersected_coords);
        remaining.axes[i] = std::move(intersected_coords);
        if(!x.axes[i].empty()) // do not include empty hc
        {
            result.push_back(std::move(x));
        }
    }
    return result;
}

search_space HC::remove_slice_if_good(const slice &s, index_t max_codim) &&
{
    if(!is_slice_good(s, max_codim))
    {
        search_space result;
        result.push_back(std::move(*this));
        return result;
    }
    return remove_slice_owned<true>(std::move(*this), s);
}


std::pair<HC, HC> HC::split(index_t n, index_t i) const
{
    assert(axes[n].contains(i));
    std::pair<HC, HC> result{*this, *this};
    result.first.axes[n] = integer_set{i};
    result.second.axes[n].erase(i);
    return result;
}


std::string HC::to_string(bool verbose) const
{
    std::ostringstream oss;
    if(verbose)
    {
        oss << "Hypercuboid with " << axes.size() << " axes:\n";
    }
    for(size_t i = 0; i < axes.size(); i++)
    {
        oss << " Axis " << i << ": ";
        oss << range_to_string(axes[i]);
        oss << "\n";
    }
    return oss.str();
}

std::string slice::to_string() const
{
    std::ostringstream oss;
    oss << "Slice with " << fixed_axes.size() << " axes fixed: \n";
    for(const auto &[k, v] : fixed_axes)
    {
        oss << "On axis " << k << ", fixing ";
        oss << range_to_string(v) << "\n";
    }
    return oss.str();
}

std::string search_space::to_string() const
{
    std::string result = "Search space: total " + std::to_string(hcs.size()) + " hypercuboids  \n";
    for(const auto &hc : hcs)
    {   
        result += hc.to_string(false);
        result += "&\n";
    }
    if(hcs.size()>0)
    {
        result.pop_back();
        result.pop_back();
    }
    return result;
}
