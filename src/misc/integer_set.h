#ifndef INTEGER_SET_H
#define INTEGER_SET_H

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <vector>
#include <cstdint>
#include <bit>
#include <iterator>
#include <type_traits>
#include <cassert>

/* dynamic integer bit-set 
store a set of non-negative integers
unlike std::bitset<N>, the size of integer_set is dynamic and can grow as needed
*/
class integer_set
{
    using block_t = uint64_t;
    // block_bits = 64
    constexpr static uint32_t block_bits = sizeof(block_t) * 8;
    // block_mask = 63 = 0b111111
    constexpr static uint32_t block_mask = block_bits - 1;
    // block_shift = 6 = log2(64)
    constexpr static uint32_t block_shift = std::bit_width(block_mask);
    std::vector<block_t> data;
public:
    using value_type = uint32_t;
    using size_type = std::size_t;
private:
    template <bool Const>
    class iterator_base
    {
        template <bool>
        friend class iterator_base;

        using set_type = std::conditional_t<Const, const integer_set, integer_set>;

        set_type *set;
        value_type block_index;
        value_type bit_index;

        /* advance_to_next:
        if the current block is data.size(), do nothing
        if the current position is set or not set, go to the next set position or end
        */
        void advance_to_next()
        {
            assert(set != nullptr);
            while(block_index < set->data.size())
            {
                if(bit_index >= block_bits)
                {
                    block_index++;
                    bit_index = 0;
                    continue;
                }

                block_t block = set->data[block_index];
                // keep bits from bit_index onward
                block &= (~static_cast<block_t>(0)) << bit_index;
                if(block != 0)
                {
                    // there is a set bit at or after bit_index
                    bit_index = static_cast<value_type>(std::countr_zero(block));
                    return;
                }
                else
                {
                    // no set bit at or after bit_index, go to the next block
                    block_index++;
                    bit_index = 0;
                }
            }
        }
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;

        iterator_base() = default;

        iterator_base(set_type *set, value_type block_index, value_type bit_index)
            : set(set), block_index(block_index), bit_index(bit_index)
        {
            advance_to_next();
        }

        template <bool OtherConst, typename = std::enable_if_t<Const && !OtherConst>>
        iterator_base(const iterator_base<OtherConst> &other)
            : set(other.set), block_index(other.block_index), bit_index(other.bit_index)
        {
        }

        value_type operator*() const
        {
            return (block_index << block_shift) | bit_index;
        }

        iterator_base &operator++()
        {
            if(set == nullptr)
            {
                return *this;
            }

            if(block_index >= set->data.size())
            {
                return *this;
            }

            bit_index++;
            advance_to_next();
            return *this;
        }

        iterator_base operator++(int)
        {
            iterator_base temp = *this;
            ++(*this);
            return temp;
        }

        template <bool OtherConst>
        bool operator==(const iterator_base<OtherConst> &other) const
        {
            return set == other.set && block_index == other.block_index && bit_index == other.bit_index;
        }

        template <bool OtherConst>
        bool operator!=(const iterator_base<OtherConst> &other) const
        {
            return !(*this == other);
        }
    };
public:
    using iterator = iterator_base<false>;
    using const_iterator = iterator_base<true>;

    integer_set() = default;
    constexpr integer_set(std::initializer_list<value_type> values);

    bool contains(value_type value) const;
    bool empty() const noexcept;
    size_type size() const noexcept;

    iterator begin() { return iterator(this, 0, 0); }
    iterator end() { return iterator(this, static_cast<value_type>(data.size()), 0); }
    const_iterator begin() const { return const_iterator(this, 0, 0); }
    const_iterator cbegin() const { return const_iterator(this, 0, 0); }
    const_iterator end() const { return const_iterator(this, static_cast<value_type>(data.size()), 0); }
    const_iterator cend() const { return const_iterator(this, static_cast<value_type>(data.size()), 0); }

    inline constexpr void insert(value_type value);
    bool erase(value_type value);
    template <typename Predicate>
    void erase_if(Predicate pred);
    template <typename UnaryOp>
    [[nodiscard]] constexpr integer_set transform(UnaryOp op) const;

    void minus(const integer_set &other);
};

template<>
struct std::iterator_traits<integer_set::iterator>
: public integer_set::iterator {
    using value_type = integer_set::value_type;
};

template<>
struct std::iterator_traits<integer_set::const_iterator>
: public integer_set::const_iterator {
    using value_type = integer_set::value_type;
};


template <typename Predicate>
void integer_set::erase_if(Predicate pred)
{
    for(value_type block_index = 0; block_index < data.size(); block_index++)
    {
        block_t &block = data[block_index];
        for(value_type bit_index = 0; bit_index < block_bits; bit_index++)
        {
            if(block & (static_cast<block_t>(1) << bit_index))
            {
                value_type value = (block_index << block_shift) | bit_index;
                if(pred(value))
                {
                    block &= ~(static_cast<block_t>(1) << bit_index);
                }
            }
        }
    }
}

template <typename UnaryOp>
constexpr integer_set integer_set::transform(UnaryOp op) const
{
    integer_set result;
    for(value_type block_index = 0; block_index < data.size(); ++block_index)
    {
        const auto &block = data[block_index];
        for(value_type i = 0; i < block_bits; i++)
        {
            if(block & (static_cast<block_t>(1) << i))
            {
                result.insert(op((block_index << block_shift) | i));
            }
        }
    }
    return result;
}

inline constexpr integer_set::integer_set(std::initializer_list<value_type> values)
{
    for(value_type value : values)
    {
        insert(value);
    }
}

inline constexpr void integer_set::insert(value_type value)
{
    size_t block_index = value >> block_shift;
    size_t bit_index = value & block_mask;
    if(block_index >= data.size())
    {
        data.resize(block_index + 1, 0);
    }
    data[block_index] |= static_cast<block_t>(1) << bit_index;
}


#endif // INTEGER_SET_H
