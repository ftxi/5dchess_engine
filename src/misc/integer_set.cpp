#include "integer_set.h"
#include <sstream>

bool integer_set::contains(value_type value) const
{
    size_t block_index = value >> block_shift;
    size_t bit_index = value & block_mask;
    if(block_index >= data.size())
    {
        return false;
    }
    return data[block_index] & (static_cast<block_t>(1) << bit_index);
}

bool integer_set::empty() const noexcept
{
    for(const block_t &block : data)
    {
        if(block != 0)
        {
            return false;
        }
    }
    return true;
}

integer_set::size_type integer_set::size() const noexcept
{
    size_type count = 0;
    for(const block_t &block : data)
    {
        count += std::popcount(block);
    }
    return count;
}

bool integer_set::intersects(const integer_set &other) const noexcept
{
    size_t min_size = std::min(data.size(), other.data.size());
    for(size_t i = 0; i < min_size; i++)
    {
        if(data[i] & other.data[i])
        {
            return true;
        }
    }
    return false;
}

bool integer_set::erase(value_type value)
{
    size_t block_index = value >> block_shift;
    size_t bit_index = value & block_mask;
    if(block_index >= data.size())
    {
        return false;
    }
    block_t &block = data[block_index];
    bool was_set = block & (static_cast<block_t>(1) << bit_index);
    block &= ~(static_cast<block_t>(1) << bit_index);
    return was_set;
}

integer_set integer_set::operator|(const integer_set &other) const
{
    integer_set result = *this;
    result |= other;
    return result;
}

integer_set integer_set::operator&(const integer_set &other) const
{
    integer_set result = *this;
    result &= other;
    return result;
}

void integer_set::minus(const integer_set &other)
{
    size_t min_size = std::min(data.size(), other.data.size());
    for(size_t i = 0; i < min_size; i++)
    {
        data[i] &= ~other.data[i];
    }
}

integer_set &integer_set::operator|=(const integer_set &other)
{
    size_t max_size = std::max(data.size(), other.data.size());
    data.resize(max_size, 0);
    for(size_t i = 0; i < other.data.size(); i++)
    {
        data[i] |= other.data[i];
    }
    return *this;
}

integer_set &integer_set::operator&=(const integer_set &other)
{
    size_t min_size = std::min(data.size(), other.data.size());
    data.resize(min_size, 0);
    for(size_t i = 0; i < min_size; i++)
    {
        data[i] &= other.data[i];
    }
    return *this;
}

std::string integer_set::to_string() const
{
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for(value_type block_index = 0; block_index < data.size(); block_index++)
    {
        const block_t &block = data[block_index];
        for(value_type bit_index = 0; bit_index < block_bits; bit_index++)
        {
            if(block & (static_cast<block_t>(1) << bit_index))
            {
                value_type value = (block_index << block_shift) | bit_index;
                if(!first)
                {
                    oss << ", ";
                }
                oss << value;
                first = false;
            }
        }
    }
    oss << "}";
    return oss.str();
}
