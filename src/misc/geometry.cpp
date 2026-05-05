#include "geometry.h"
#include <cassert>
#include <sstream>
#include "utils.h"

HC::HC(std::initializer_list<integer_set> init_axes)
    : axes(init_axes)
{
}

HC::HC(std::vector<integer_set> &&init_axes)
    : axes(std::move(init_axes))
{
}

const integer_set &HC::operator[](size_t i) const
{
    return axes[i];
}

integer_set &HC::operator[](size_t i)
{
    return axes[i];
}

bool HC::contains(point loc) const
{
    assert(loc.size() == axes.size());
    for(index_t i = 0; i < static_cast<index_t>(axes.size()); i++)
    {
        if(!axes[i].contains(loc[i]))
            return false;
    }
    return true;
}

bool HC::empty() const
{
    for(const auto& axis : axes)
    {
        if(axis.empty())
            return true;
    }
    return false;
}

size_t HC::volume() const
{
    size_t result = 1;
    for(const auto& axis : axes)
    {
        result *= axis.size();
    }
    return result;
}

bool slice::contains(const point &p) const
{
    for(auto [n, coords] : fixed_axes)
    {
        if(!coords.contains(p[n]))
            return false;
    }
    return true;
}

size_t search_space::volume() const
{
    size_t result = 0;
    for(const auto& hc : hcs)
    {
        result += hc.volume();
    }
    return result;
}

bool search_space::empty() const
{
    for(const auto& hc : hcs)
    {
        if(!hc.empty())
            return false;
    }
    return true;
}

bool search_space::contains(point loc) const
{
    for(const auto& hc : hcs)
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

HC &search_space::back()
{
    return hcs.back();
}

const HC &search_space::back() const
{
    return hcs.back();
}

void search_space::pop_back()
{
    hcs.pop_back();
}

std::list<HC>::iterator search_space::begin()
{
    return hcs.begin();
}

std::list<HC>::iterator search_space::end()
{
    return hcs.end();
}

std::list<HC>::const_iterator search_space::begin() const
{
    return hcs.begin();
}

std::list<HC>::const_iterator search_space::end() const
{
    return hcs.end();
}

void search_space::concat(search_space &&other)
{
    hcs.splice(hcs.end(), other.hcs);
}

void search_space::prune_empty()
{
    hcs.remove_if([](const HC &hc) {
        return hc.empty();
    });
}

search_space HC::remove_slice(const slice &s) const
{
    search_space result;
    HC remaining = *this;
    for(const auto& [i, fixed_coords] : s.fixed_axes)
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

search_space HC::remove_point(const point &p) const
{
    search_space result;
    HC remaining = *this;
    for(index_t i = 0; i < static_cast<index_t>(p.size()); i++)
    {
        HC x = remaining;
        x.axes[i].erase(p[i]);
        integer_set singleton = {p[i]};
        remaining.axes[i] = singleton;
        if(!x.axes[i].empty()) // do not include empty hc
        {
            result.push_back(std::move(x));
        }
    }
    return result;
}

search_space HC::remove_slice_carefully(const slice &s) const
{
    search_space result;
    HC remaining = *this;
    slice s1 = s;
    for(auto& [i, fixed_coords] : s1.fixed_axes)
    {
        if(!remaining.axes[i].intersects(fixed_coords))
        {
            return search_space({{*this}});
        }
        else
        {
            fixed_coords &= remaining.axes[i];
            assert(!fixed_coords.empty());
        }
    }
    for(const auto& [i, fixed_coords] : s1.fixed_axes)
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
    for(const auto& [k, v] : fixed_axes)
    {
        oss << "On axis " << k << ", fixing ";
        oss << range_to_string(v) << "\n";
    }
    return oss.str();
}

std::string search_space::to_string() const
{
    std::string result = "Search space: total " + std::to_string(hcs.size()) + " hypercuboids  \n";
    for(const auto& hc : hcs)
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
