#include "integer_set.h"


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

void integer_set::minus(const integer_set &other)
{
    size_t min_size = std::min(data.size(), other.data.size());
    for(size_t i = 0; i < min_size; i++)
    {
        data[i] &= ~other.data[i];
    }
}
