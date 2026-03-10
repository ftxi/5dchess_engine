#ifndef INTEGER_SET_H
#define INTEGER_SET_H

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <vector>

class integer_set
{
    std::vector<int> data;
public:
    using value_type = int;
    using size_type = std::size_t;
    using iterator = std::vector<int>::iterator;
    using const_iterator = std::vector<int>::const_iterator;

    integer_set() = default;
    constexpr integer_set(std::initializer_list<int> values);

    bool contains(int value) const;
    bool empty() const noexcept;
    size_type size() const noexcept;

    iterator begin();
    const_iterator begin() const;
    const_iterator cbegin() const;
    iterator end();
    const_iterator end() const;
    const_iterator cend() const;

    const_iterator find(int value) const;

    iterator insert(int value);
    iterator insert(const_iterator hint, int value);
    bool erase(int value);
    template <typename Predicate>
    void erase_if(Predicate pred);
    template <typename UnaryOp>
    constexpr void transform(UnaryOp op);

    void minus(const integer_set &other);
};

template <typename Predicate>
void integer_set::erase_if(Predicate pred)
{
    data.erase(std::remove_if(data.begin(), data.end(), pred), data.end());
}

template <typename UnaryOp>
constexpr void integer_set::transform(UnaryOp op)
{
    for(auto &value : data)
    {
        value = op(value);
    }
    std::sort(data.begin(), data.end());
    data.erase(std::unique(data.begin(), data.end()), data.end());
}

inline constexpr integer_set::integer_set(std::initializer_list<int> values)
    : data(values)
{
    std::sort(data.begin(), data.end());
    data.erase(std::unique(data.begin(), data.end()), data.end());
}

#endif // INTEGER_SET_H
