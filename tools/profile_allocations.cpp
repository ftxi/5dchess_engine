#include "hc_profile_metrics.h"

#ifdef HC_PROFILE_METRICS
#include <cstdlib>
#include <limits>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace
{
void *allocate(std::size_t size, std::size_t alignment = 0)
{
    if(size == 0) size = 1;
    bool representable = true;
    if(alignment)
    {
        const auto padding = alignment - 1;
        representable = size <= std::numeric_limits<std::size_t>::max() - padding;
        if(representable) size = (size + padding) & ~padding;
    }
    while(true)
    {
        void *result;
        if(!representable) result = nullptr;
        else if(!alignment) result = std::malloc(size);
#ifdef _WIN32
        else result = _aligned_malloc(size, alignment);
#else
        else result = std::aligned_alloc(alignment, size);
#endif
        if(result)
        {
            hc_profile::note_allocation();
            return result;
        }
        const auto handler = std::get_new_handler();
        if(!handler) throw std::bad_alloc();
        handler();
    }
}

void free_aligned(void *p) noexcept
{
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} /* namespace */

void *operator new(std::size_t n) { return allocate(n); }
void *operator new[](std::size_t n) { return allocate(n); }
void *operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void *operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
    try { return allocate(n); } catch(...) { return nullptr; }
}
void *operator new[](std::size_t n, const std::nothrow_t &t) noexcept { return ::operator new(n, t); }
void *operator new(std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept
{
    try { return allocate(n, static_cast<std::size_t>(a)); } catch(...) { return nullptr; }
}
void *operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t &t) noexcept
{
    return ::operator new(n, a, t);
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t) noexcept { free_aligned(p); }
void operator delete[](void *p, std::align_val_t) noexcept { free_aligned(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { free_aligned(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { free_aligned(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { free_aligned(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { free_aligned(p); }
#endif
