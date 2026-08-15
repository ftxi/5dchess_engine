#include "statistics.h"
#include <algorithm>

namespace
{
int interval_size(int a_min, int a_max, int b_min, int b_max)
{
    return std::max(0, std::min(a_max, b_max) - std::max(a_min, b_min) + 1);
}
}

timeline_data count_timelines(const state &s)
{
    const bool player = s.get_present().second;
    const auto [l_min, l_max] = s.get_lines_range();
    const auto [l0_min, l0_max] = s.get_initial_lines_range();
    const auto [active_min, active_max] = s.get_active_range();
    const auto [mandatory_timelines, optional_timelines, unplayable_timelines]
        = s.get_timeline_status();

    const int total = l_max - l_min + 1;
    const int active = active_max - active_min + 1;
    const int inactive = total - active;

    const int mandatory = static_cast<int>(mandatory_timelines.size());
    const int optional = static_cast<int>(optional_timelines.size());
    const int unplayable = static_cast<int>(unplayable_timelines.size());
    const int playable = mandatory + optional;

    const int white_created = l_max - l0_max;
    const int black_created = l0_min - l_min;
    const int friendly_created = player ? black_created : white_created;
    const int hostile_created = player ? white_created : black_created;
    const int timeline_advantage = hostile_created - friendly_created;
    const int active_timeline_allowance = std::max(0, timeline_advantage + 1);

    const int white_active_created
        = interval_size(active_min, active_max, l0_max + 1, l_max);
    const int black_active_created
        = interval_size(active_min, active_max, l_min, l0_min - 1);
    const int friendly_active_created
        = player ? black_active_created : white_active_created;
    const int hostile_active_created
        = player ? white_active_created : black_active_created;

    return timeline_data{
        total,
        active,
        inactive,
        mandatory,
        optional,
        unplayable,
        playable,
        friendly_created,
        hostile_created,
        timeline_advantage,
        active_timeline_allowance,
        friendly_active_created,
        hostile_active_created
    };
}

template<timelines_status S>
material_data<int> count_material_sum(const state &s)
{
    int lpawn = 0;
    int lknight = 0;
    int lrook = 0;
    int lbishop = 0;
    int lunicorn = 0;
    int ldragon = 0;
    int queen = 0;
    int royal = 0;
    const auto [mandatory_timelines, optional_timelines, unplayable_timelines] = s.get_timeline_status();
    std::vector<int> lines = {};
    if constexpr (static_cast<bool>(S & timelines_status::MANDATORY))
    {
        lines = mandatory_timelines;
    }
    if constexpr (static_cast<bool>(S & timelines_status::OPTIONAL))
    {
        lines.insert(lines.end(), optional_timelines.begin(), optional_timelines.end());
    }
    if constexpr (static_cast<bool>(S & timelines_status::UNPLAYABLE))
    {
        lines.insert(lines.end(), unplayable_timelines.begin(), unplayable_timelines.end());
    }
    for(int line: lines)
    {
        auto [t, c] = s.get_timeline_end(line);
        std::shared_ptr<board> b = s.get_board(line, t, c);
        lpawn += std::popcount(b->lpawn());
        lknight += std::popcount(b->lknight());
        lrook += std::popcount(b->lrook());
        lbishop += std::popcount(b->lbishop());
        lunicorn += std::popcount(b->lunicorn());
        ldragon += std::popcount(b->ldragon());
        queen += std::popcount(b->queen());
        royal += std::popcount(b->royal());
    }
    return material_data<int>{{
        lpawn, lknight, lrook, lbishop, lunicorn, ldragon, queen, royal
    }};
}

template <timelines_status S>
material_data<int> count_material_diff(const state &s)
{
    int lpawn = 0;
    int lknight = 0;
    int lrook = 0;
    int lbishop = 0;
    int lunicorn = 0;
    int ldragon = 0;
    int queen = 0;
    int royal = 0;
    const auto [mandatory_timelines, optional_timelines, unplayable_timelines] = s.get_timeline_status();
    std::vector<int> lines = {};
    if constexpr (static_cast<bool>(S & timelines_status::MANDATORY))
    {
        lines = mandatory_timelines;
    }
    if constexpr (static_cast<bool>(S & timelines_status::OPTIONAL))
    {
        lines.insert(lines.end(), optional_timelines.begin(), optional_timelines.end());
    }
    if constexpr (static_cast<bool>(S & timelines_status::UNPLAYABLE))
    {
        lines.insert(lines.end(), unplayable_timelines.begin(), unplayable_timelines.end());
    }
    for(int line: lines)
    {
        auto [t, c] = s.get_timeline_end(line);
        std::shared_ptr<board> b = s.get_board(line, t, c);
        bitboard_t friendly = c ? b->black() : b->white();
        bitboard_t hostile = c ? b->white() : b->black();
        lpawn += std::popcount(b->lpawn() & friendly) - std::popcount(b->lpawn() & hostile);
        lknight += std::popcount(b->lknight() & friendly) - std::popcount(b->lknight() & hostile);
        lrook += std::popcount(b->lrook() & friendly) - std::popcount(b->lrook() & hostile);
        lbishop += std::popcount(b->lbishop() & friendly) - std::popcount(b->lbishop() & hostile);
        lunicorn += std::popcount(b->lunicorn() & friendly) - std::popcount(b->lunicorn() & hostile);
        ldragon += std::popcount(b->ldragon() & friendly) - std::popcount(b->ldragon() & hostile);
        queen += std::popcount(b->queen() & friendly) - std::popcount(b->queen() & hostile);
        royal += std::popcount(b->royal() & friendly) - std::popcount(b->royal() & hostile);
    }
    return material_data<int>{{
        lpawn, lknight, lrook, lbishop, lunicorn, ldragon, queen, royal
    }};
}

template material_data<int> count_material_sum<timelines_status::NONE>(const state &);
template material_data<int> count_material_sum<timelines_status::MANDATORY>(const state &);
template material_data<int> count_material_sum<timelines_status::OPTIONAL>(const state &);
template material_data<int> count_material_sum<timelines_status::UNPLAYABLE>(const state &);
template material_data<int> count_material_sum<timelines_status::MANDATORY | timelines_status::OPTIONAL>(const state &);
template material_data<int> count_material_sum<timelines_status::MANDATORY | timelines_status::UNPLAYABLE>(const state &);
template material_data<int> count_material_sum<timelines_status::OPTIONAL | timelines_status::UNPLAYABLE>(const state &);
template material_data<int> count_material_sum<timelines_status::MANDATORY | timelines_status::OPTIONAL | timelines_status::UNPLAYABLE>(const state &);

template material_data<int> count_material_diff<timelines_status::NONE>(const state &);
template material_data<int> count_material_diff<timelines_status::MANDATORY>(const state &);
template material_data<int> count_material_diff<timelines_status::OPTIONAL>(const state &);
template material_data<int> count_material_diff<timelines_status::UNPLAYABLE>(const state &);
template material_data<int> count_material_diff<timelines_status::MANDATORY | timelines_status::OPTIONAL>(const state &);
template material_data<int> count_material_diff<timelines_status::MANDATORY | timelines_status::UNPLAYABLE>(const state &);
template material_data<int> count_material_diff<timelines_status::OPTIONAL | timelines_status::UNPLAYABLE>(const state &);
template material_data<int> count_material_diff<timelines_status::MANDATORY | timelines_status::OPTIONAL | timelines_status::UNPLAYABLE>(const state &);
