#ifndef HC_PROFILE_METRICS_H
#define HC_PROFILE_METRICS_H

#include <cstddef>

namespace hc_profile
{
struct counters
{
    std::size_t allocations = 0;
    std::size_t live_hcs = 0;
    std::size_t peak_live_hcs = 0;
};

constexpr bool available =
#ifdef HC_PROFILE_METRICS
    true;
#else
    false;
#endif

// One thread-local diagnostic search at a time. Ending a measurement never
// leaves pointers in HCs; lifetime tickets from previous sessions are ignored.
class measurement
{
    std::size_t ticket;
    bool finished = false;
public:
    measurement();
    measurement(const measurement &) = delete;
    measurement &operator=(const measurement &) = delete;
    ~measurement();
    counters finish();
};

void note_allocation() noexcept;

#ifdef HC_PROFILE_METRICS
class hc_lifetime
{
    std::size_t ticket;
public:
    hc_lifetime() noexcept;
    hc_lifetime(const hc_lifetime &) noexcept : hc_lifetime() {}
    hc_lifetime(hc_lifetime &&) noexcept : hc_lifetime() {}
    hc_lifetime &operator=(const hc_lifetime &) noexcept { return *this; }
    hc_lifetime &operator=(hc_lifetime &&) noexcept { return *this; }
    ~hc_lifetime();
};
#endif
} /* namespace hc_profile */

#endif /* HC_PROFILE_METRICS_H */
