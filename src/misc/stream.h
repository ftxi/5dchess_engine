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
        std::unique_ptr<stream<T>> rest;
        std::function<auto ()->std::unique_ptr<stream<T>>> thunk;
        cons_wrapper(T f, std::unique_ptr<stream<T>> r, std::function<auto ()->std::unique_ptr<stream<T>>> t)
        : first{std::move(f)}, rest{std::move(r)}, thunk{std::move(t)} {}
        cons_wrapper(const cons_wrapper& other)
            : first(other.first),
              rest(nullptr),  // Initialize as null
              thunk(other.thunk)
        {
        }
        friend void swap(cons_wrapper& a, cons_wrapper& b)
        {
            std::swap(a.first, b.first);
            std::swap(a.rest, b.rest);
            std::swap(a.thunk, b.thunk);
        }
        cons_wrapper& operator=(cons_wrapper& other)
        {
            swap(*this, other);
            return *this;
        }
    };
public:
    std::optional<cons_wrapper> v;
public:
    stream() {}
    stream(T x, std::function<auto ()->std::unique_ptr<stream<T>>> f)
    :v {std::make_optional<cons_wrapper>(x, nullptr, f)} {}
    friend void swap(stream<T>& a, stream<T>& b)
    {
        std::swap(a.v, b.v);
    }
    //copy operations
    stream(stream& other) {
//        }
        if (other.v) {
            // Either construct new value or assign to existing one
            if (v) {
                //std::cerr << "copy";
                v.value() = other.v.value();  // Uses your cons_wrapper's copy assignment
            } else {
                //std::cerr << "emplace";
                v.emplace(other.v.value());   // Uses your cons_wrapper's copy constructor
            }
        } else {
            //std::cerr << "reset";
            v.reset();  // Clear our value if other is empty
        }
        //std::cerr << "\n";
    }
    stream& operator=(stream other) {  // Copy-and-swap idiom
        swap(*this, other);
        return *this;
    }
    //move operations
    stream(stream&&) = default;
    stream& operator=(stream&&) = default;
    
    bool empty() const
    {
        return !v.has_value();
    }
    T car() const
    {
        return v.value().first;
    }
    stream<T>& cdr()
    {
        cons_wrapper& node = v.value();
        if(!node.rest)
        {
            node.rest = node.thunk();
        }
        return *node.rest;
    }
    ~stream()
    {
        if (v.has_value() && v.value().rest)
        {
            // Iterative destruction to prevent stack overflow
            auto current = std::move(v.value().rest);
            while (current && current->v.has_value() && current->v.value().rest)
            {
                auto next = std::move(current->v.value().rest);
                current = std::move(next);
            }
            // All unique_ptrs have been moved and will be destroyed in reverse order
        }
    }
};

template<typename T>
stream<T> snil()
{
    return stream<T>();
}

#define scons(x,y) stream<decltype(x)>((x), [=](){return std::make_unique<stream<decltype(x)>>(std::move(y));})

template<typename T>
stream<T> take(int n, stream<T> s)
{
    if (n <= 0 || s.empty())
    {
        return snil<T>();
    }

    // Capture the current head immediately
    T current_head = s.car();
    
    // Move the remaining stream into a shared_ptr for the lambda capture
    auto remaining = std::make_shared<stream<T>>(s.cdr());
    
    return stream<T>(
        std::move(current_head),
        [n, remaining]() -> std::unique_ptr<stream<T>> {
            return std::make_unique<stream<T>>(take(n - 1, *remaining));
        }
    );
}

template<typename T>
stream<T> map(std::function<auto(T)->T> f, stream<T> s)
{
    if(s.empty())
    {
        return snil<T>();
    }
    auto remaining = std::make_shared<stream<T>>(s.cdr());
    return stream<T>(
        std::move(f(s.car())),
        [f, remaining](){
            return std::make_unique<stream<T>>(map(f, *remaining));
    });
}

stream<int> naturals(int start=0)
{
    return scons(start, naturals(start+1));
}

template<typename T>
void print_stream(stream<T> s)
{
    while(!s.empty())
    {
        std::cout << s.car() << " ";
        stream<int> t = s.cdr();
        s = t;
    }
    std::cout << std::endl;
}

#endif // STREAM_H
