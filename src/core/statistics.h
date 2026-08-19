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
    constexpr static int COUNT = 13;
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

/* Candidate-action counts for the player to move.

   These are logarithms of hypercuboid volumes rather than counts of legal
   actions.  The non-new volume restricts every possible new-timeline axis to
   its null coordinate.
 */
struct move_count_data
{
    float log_universe_volume;
    float log_non_new_volume;
    constexpr static int COUNT = 2;
};

move_count_data count_move_space(const state &s);

/* Royal safety, from the perspective of the player to move.

   An exposure belongs to the player whose piece could use the ray to attack an
   opposing royal piece.  T_PLUS is accumulated at timeline ends.  The six
   L-related directions are accumulated at every stored board because a board
   in history can become relevant as neighboring timelines advance.  Their L
   sign is normalized to the player-to-move's White-oriented frame: raw L
   directions are reversed when Black is to move.
 */
struct royal_safety_data
{
    enum exposure_direction
    {
        T_PLUS,
        T_PLUS_HISTORICAL,
        L_PLUS,
        L_MINUS,
        L_PLUS_T_PLUS,
        L_PLUS_T_MINUS,
        L_MINUS_T_PLUS,
        L_MINUS_T_MINUS,
        EXPOSURE_COUNT
    };

    std::array<int, EXPOSURE_COUNT> friendly_exposure{};
    std::array<int, EXPOSURE_COUNT> hostile_exposure{};

    int friendly_checks = 0;
    int hostile_checks = 0;
    int friendly_strong_checks = 0;
    int hostile_strong_checks = 0;
};

royal_safety_data count_royal_safety(const state &s);

#endif /* STATISTICS_H */
