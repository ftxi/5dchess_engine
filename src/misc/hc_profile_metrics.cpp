#include "hc_profile_metrics.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace hc_profile
{
namespace
{
thread_local counters totals;
thread_local std::size_t generation = 0;
thread_local bool active = false;
} /* namespace */

measurement::measurement()
{
    if(!available)
        throw std::logic_error("metrics require a build with HC_PROFILE_METRICS=ON");
    if(active) throw std::logic_error("nested HC profile measurements are not supported");
    if(++generation == 0) ++generation;
    ticket = generation;
    totals = {};
    active = true;
}

measurement::~measurement()
{
    if(!finished && generation == ticket) active = false;
}

counters measurement::finish()
{
    if(finished || !active || generation != ticket)
        throw std::logic_error("HC profile measurement is no longer active");
    finished = true;
    active = false;
    return totals;
}

void note_allocation() noexcept
{
    if(active) ++totals.allocations;
}

#ifdef HC_PROFILE_METRICS
hc_lifetime::hc_lifetime() noexcept : ticket(active ? generation : 0)
{
    if(ticket)
    {
        ++totals.live_hcs;
        totals.peak_live_hcs = std::max(totals.peak_live_hcs, totals.live_hcs);
    }
}

hc_lifetime::~hc_lifetime()
{
    if(ticket && active && ticket == generation)
    {
        assert(totals.live_hcs > 0);
        --totals.live_hcs;
    }
}
#endif
} /* namespace hc_profile */
