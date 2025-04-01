#ifndef STREAM_H
#define STREAM_H

#include <iostream>

#include <functional>
#include <optional>
#include <memory>

template<typename T>
class stream
{
    struct cons_wrapper {
        T first;
        std::shared_ptr<stream<T>> rest;
        std::function<auto ()->std::shared_ptr<stream<T>>> thunk;
        cons_wrapper(T f, std::shared_ptr<stream<T>> r, std::function<auto ()->std::shared_ptr<stream<T>>> t)
        : first{std::move(f)}, rest{std::move(r)}, thunk{std::move(t)} {}
//        ~cons_wrapper()
//        {
//            std::cerr << "cons(" << first << ",...) is freed\n";
//        }
    };
public:
    std::optional<cons_wrapper> v;
public:
    stream() {}
    stream(T x, std::function<auto ()->std::shared_ptr<stream<T>>> f)
    :v {std::make_optional<cons_wrapper>(x, nullptr, f)} {}
    
    
    bool empty() const
    {
        return !v.has_value();
    }
    T car() const
    {
        return v.value().first;
    }
    std::shared_ptr<stream<T>>& ptr_cdr()
    {
        cons_wrapper& node = v.value();
        if(!node.rest)
        {
            node.rest = node.thunk();
        }
        return node.rest;
    }
    stream<T>& cdr()
    {
        return *ptr_cdr();
    }
    class iterator
    {
        stream<T>* current = nullptr;

    public:
        // Iterator traits (required for C++ iterators)
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() = default;
        explicit iterator(stream<T>& s) : current(&s) {}

        // Dereference operator
        T operator*() const {
            return current->car();
        }

        // Arrow operator
        T* operator->() const {
            // Note: This is somewhat artificial for streams
            // since we generate values on demand
            static thread_local T tmp;
            tmp = current->car();
            return &tmp;
        }

        // Prefix increment
        iterator& operator++() {
            if (current && !current->empty()) {
                current = &(current->cdr());
                if (current->empty()) {
                    current = nullptr;
                }
            }
            return *this;
        }

        // Postfix increment
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        // Equality comparison
        bool operator==(const iterator& other) const {
            return current == other.current;
        }

        // C++20: Default inequality operator generated automatically
    };
    iterator begin() {
        if (empty()) return end();
        return iterator(*this);
    }

    iterator end() {
        return iterator();
    }
};

template<typename T>
stream<T> snil()
{
    return stream<T>();
}

#define scons(x,y) stream<decltype(x)>((x), [=](){return std::make_shared<stream<decltype(x)>>(std::move(y));})

stream<int> naturals(int start=0)
{
    return scons(start, naturals(start+1));
}

template<typename T>
stream<T> take(int n, stream<T> s)
{
    if(n == 0 || s.empty())
        return snil<T>();
    
    //std::cerr << "taking elements, " << n << " remaining, current is " << s.car() << "\n";
    return stream<T>(s.car(), [n,rest=s.ptr_cdr()](){
        return std::make_shared<stream<T>>(take(n-1, *rest));
    });
}

template<typename T>
stream<T> map(std::function<auto (T)->T> f, stream<T> s)
{
    if(s.empty())
        return snil<T>();
    return stream<T>(f(s.car()), [f,rest=s.ptr_cdr()](){
        return std::make_shared<stream<T>>(map(f, *rest));
    });
}

template<typename T>
stream<T> filter(std::function<auto (T)->bool> f, stream<T> s)
{
    if(s.empty())
    {
        return snil<T>();
    }
    else if(f(s.car()))
    {
        return stream<T>(s.car(), [f,rest=s.ptr_cdr()](){
            return std::make_shared<stream<T>>(filter(f, *rest));
        });
    }
    else
    {
        return filter(f, s.cdr());
    }
}

#endif // STREAM_H
