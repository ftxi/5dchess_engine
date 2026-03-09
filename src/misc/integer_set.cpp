#include "integer_set.h"

#include <algorithm>

bool integer_set::contains(int value) const
{
    return std::binary_search(data.begin(), data.end(), value);
}

bool integer_set::empty() const noexcept
{
    return data.empty();
}

integer_set::size_type integer_set::size() const noexcept
{
    return data.size();
}

integer_set::iterator integer_set::begin()
{
    return data.begin();
}

integer_set::const_iterator integer_set::begin() const
{
    return data.begin();
}

integer_set::const_iterator integer_set::cbegin() const
{
    return data.cbegin();
}

integer_set::iterator integer_set::end()
{
    return data.end();
}

integer_set::const_iterator integer_set::end() const
{
    return data.end();
}

integer_set::const_iterator integer_set::cend() const
{
    return data.cend();
}

integer_set::iterator integer_set::insert(int value)
{
    auto position = std::lower_bound(data.begin(), data.end(), value);
    if(position != data.end() && *position == value)
    {
        return position;
    }
    return data.insert(position, value);
}

integer_set::iterator integer_set::insert(const_iterator /*hint*/, int value)
{
    return insert(value);
}

bool integer_set::erase(int value)
{
    auto position = std::lower_bound(data.begin(), data.end(), value);
    if(position == data.end() || *position != value)
    {
        return false;
    }
    data.erase(position);
    return true;
}

integer_set::const_iterator integer_set::find(int value) const
{
    auto position = std::lower_bound(data.begin(), data.end(), value);
    if(position == data.end() || *position != value)
    {
        return data.end();
    }
    return position;
}

void integer_set::minus(const integer_set &other)
{
    auto junk_start = data.begin();
    auto it_a = data.begin();
    auto end_a = data.end();
    auto it_b = other.cbegin();
    auto end_b = other.cend();

    while(it_a != end_a && it_b != end_b)
    {
        if(*it_a < *it_b)
        {
            *junk_start++ = *it_a++;
        }
        else if(*it_b < *it_a)
        {
            ++it_b;
        }
        else
        {
            ++it_a;
            ++it_b;
        }
    }

    while(it_a != end_a)
    {
        *junk_start++ = *it_a++;
    }

    data.erase(junk_start, data.end());
}
