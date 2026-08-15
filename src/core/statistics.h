#ifndef STATISTICS_H
#define STATISTICS_H

#include <array>
#include "state.h"
#include "utils.h"

/* Material counts */

template<typename T>
struct material_data
{
    enum indices
    {
        LPAWN,
        LKNIGHT,
        LROOK,
        LBISHOP,
        LUNICORN,
        LDRAGON,
        QUEEN,
        ROYAL,
        COUNT
    };

    std::array<T, COUNT> values;
};

/* Timeline counts, from the perspective of the player to move. */

struct timeline_data
{
    int total;
    int active;
    int inactive;

    int mandatory;
    int optional;
    int unplayable;
    int playable;

    int friendly_created;
    int hostile_created;
    // Positive means the player to move has created fewer timelines.
    int timeline_advantage;
    int active_timeline_allowance;

    int friendly_active_created;
    int hostile_active_created;
};

enum class timelines_status {
    NONE = 0,
    MANDATORY = 1,
    OPTIONAL = 1 << 1,
    UNPLAYABLE = 1 << 2
};

template <>
inline constexpr bool enable_bitmask_operators<timelines_status> = true;

template<timelines_status S>
material_data<int> count_material_sum(const state &s);

template<timelines_status S>
material_data<int> count_material_diff(const state &s);

timeline_data count_timelines(const state &s);

#endif /* STATISTICS_H */
