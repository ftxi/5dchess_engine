#include "integer_set.h"
#include <sstream>

bool integer_set::contains(value_type value) const
{
    size_t block_index = value >> block_shift;
    size_t bit_index = value & block_mask;
    if(block_index >= block_count())
    {
        return false;
    }
    return block_at(block_index) & (static_cast<block_t>(1) << bit_index);
}

bool integer_set::empty() const noexcept
{
    if(first_block != 0)
    {
        return false;
    }
    return std::all_of(
        remaining_blocks.begin(),
        remaining_blocks.end(),
        [](block_t block) { return block == 0; });
}

integer_set::size_type integer_set::size() const noexcept
{
    size_type count = std::popcount(first_block);
    for(const block_t &block : remaining_blocks)
    {
        count += std::popcount(block);
    }
    return count;
}

bool integer_set::intersects(const integer_set &other) const noexcept
{
    if(first_block & other.first_block)
    {
        return true;
    }
    const size_t min_size
        = std::min(remaining_blocks.size(), other.remaining_blocks.size());
    for(size_t i = 0; i < min_size; i++)
    {
        if(remaining_blocks[i] & other.remaining_blocks[i])
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
    if(block_index >= block_count())
    {
        return false;
    }
    block_t &block = block_at(block_index);
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
    first_block &= ~other.first_block;
    const size_t min_size
        = std::min(remaining_blocks.size(), other.remaining_blocks.size());
    for(size_t i = 0; i < min_size; i++)
    {
        remaining_blocks[i] &= ~other.remaining_blocks[i];
    }
}

integer_set &integer_set::operator|=(const integer_set &other)
{
    first_block |= other.first_block;
    ensure_block_count(other.block_count());
    for(size_t i = 0; i < other.remaining_blocks.size(); i++)
    {
        remaining_blocks[i] |= other.remaining_blocks[i];
    }
    return *this;
}

integer_set &integer_set::operator&=(const integer_set &other)
{
    first_block &= other.first_block;
    const size_t min_size
        = std::min(remaining_blocks.size(), other.remaining_blocks.size());
    remaining_blocks.resize(min_size);
    for(size_t i = 0; i < min_size; i++)
    {
        remaining_blocks[i] &= other.remaining_blocks[i];
    }
    return *this;
}

std::string integer_set::to_string() const
{
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for(value_type block_index = 0; block_index < block_count(); block_index++)
    {
        const block_t &block = block_at(block_index);
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
