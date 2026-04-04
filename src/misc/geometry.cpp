#include "geometry.h"
#include <cassert>
#include <sstream>
#include "utils.h"

const integer_set &HC::operator[](size_t i) const
{
    return axes[i];
}

bool HC::contains(point loc) const
{
    assert(loc.size() == axes.size());
    for(uint32_t i = 0; i < static_cast<uint32_t>(axes.size()); i++)
    {
        if(!axes[i].contains(loc[i]))
            return false;
    }
    return true;
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

bool search_space::contains(point loc) const
{
    for(const auto& hc : hcs)
    {
        if(hc.contains(loc))
            return true;
    }
    return false;
}

void search_space::concat(search_space &&other)
{
    hcs.splice(hcs.end(), other.hcs);
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
            result.hcs.push_back(std::move(x));
        }
    }
    return result;
}

search_space HC::remove_point(const point &p) const
{
    search_space result;
    HC remaining = *this;
    for(uint32_t i = 0; i < static_cast<uint32_t>(p.size()); i++)
    {
        HC x = remaining;
        x.axes[i].erase(p[i]);
        integer_set singleton = {p[i]};
        remaining.axes[i] = singleton;
        if(!x.axes[i].empty()) // do not include empty hc
        {
            result.hcs.push_back(std::move(x));
        }
    }
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
