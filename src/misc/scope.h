#ifndef SCOPE_H
#define SCOPE_H
#include <functional>

/*
A simple RAII-style scope guard that executes a given function when it goes out of scope. Similar to std::scope_exit in C++23, which is unfortunately not currently available in all compilers.
*/

class scope_exit_guard
{
    std::function<auto () -> void> exit_action;
public:
    explicit scope_exit_guard(std::function<auto () -> void> action) : exit_action(std::move(action)) {}
    ~scope_exit_guard()
    {
        if(exit_action)
        {
            exit_action();
        }
    }
    scope_exit_guard(const scope_exit_guard&) = delete;
    scope_exit_guard& operator=(const scope_exit_guard&) = delete;
    scope_exit_guard(scope_exit_guard&& other) noexcept : exit_action(std::move(other.exit_action)) {}
    scope_exit_guard& operator=(scope_exit_guard&& other) noexcept
    {
        if(this != &other)
        {
            exit_action = std::move(other.exit_action);
        }
        return *this;
    }
    void dismiss()
    {
        exit_action = nullptr;
    }
};

#endif /* SCOPE_H */
