#include "statistics.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "hypercuboid.h"

namespace
{
int interval_size(int a_min, int a_max, int b_min, int b_max)
{
    return std::max(0, std::min(a_max, b_max) - std::max(a_min, b_min) + 1);
}

int turn_index(int t, bool c)
{
    return 2 * t + static_cast<int>(c);
}

turn_t index_turn(int v)
{
    return {v >> 1, static_cast<bool>(v & 1)};
}

struct board_coordinate
{
    int l;
    int v;

    auto operator<=>(const board_coordinate &) const = default;
};

struct exposure_direction
{
    int dl;
    int dv;
    royal_safety_data::exposure_direction feature;
};

struct exposure_board
{
    board_coordinate coordinate;
    std::shared_ptr<board> value;
};

royal_safety_data::exposure_direction orient_l_direction(
    royal_safety_data::exposure_direction feature,
    bool player)
{
    using direction_t = royal_safety_data::exposure_direction;
    if(!player)
    {
        return feature;
    }
    switch(feature)
    {
        case direction_t::L_PLUS: return direction_t::L_MINUS;
        case direction_t::L_MINUS: return direction_t::L_PLUS;
        case direction_t::L_PLUS_T_PLUS: return direction_t::L_MINUS_T_PLUS;
        case direction_t::L_PLUS_T_MINUS: return direction_t::L_MINUS_T_MINUS;
        case direction_t::L_MINUS_T_PLUS: return direction_t::L_PLUS_T_PLUS;
        case direction_t::L_MINUS_T_MINUS: return direction_t::L_PLUS_T_MINUS;
        default: return feature;
    }
}

bitboard_t hostile_royals(const board &b, bool attacker)
{
    return b.royal() & (attacker ? b.hostile<true>() : b.hostile<false>());
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

move_count_data count_move_space(const state &s)
{
    auto [info, search_space] = HC_info::build_HC(s);
    (void)search_space;

    double log_universe_volume = 0.0;
    double log_non_new_volume = 0.0;
    for(index_t axis = 0; axis < info.dimension; ++axis)
    {
        // Every universe axis contains at least its null coordinate.  Summing
        // logarithms avoids overflowing HC::volume() for large multiverses.
        const double log_axis_size = std::log(
            static_cast<double>(info.universe[axis].size()));
        log_universe_volume += log_axis_size;
        if(axis < info.new_axis)
        {
            log_non_new_volume += log_axis_size;
        }
    }

    return move_count_data{
        static_cast<float>(log_universe_volume),
        static_cast<float>(log_non_new_volume)
    };
}

namespace
{
using exposure_array_t
    = std::array<int, royal_safety_data::EXPOSURE_COUNT>;
using exposure_pair_t = std::pair<exposure_array_t, exposure_array_t>;

exposure_pair_t count_t_plus_exposure(const state &s, bool player)
{
    exposure_pair_t result{};
    const auto [l_min, l_max] = s.get_lines_range();
    for(int l = l_min; l <= l_max; ++l)
    {
        const auto start = s.get_timeline_start(l);
        const auto end = s.get_timeline_end(l);
        const int end_v = turn_index(end.first, end.second);
        int v = turn_index(start.first, start.second);
        if(static_cast<bool>(v & 1) != end.second)
        {
            ++v;
        }

        bitboard_t exposed = 0;
        bitboard_t end_royals = 0;
        for(; v <= end_v; v += 2)
        {
            const auto [t, c] = index_turn(v);
            const std::shared_ptr<board> b = s.get_board(l, t, c);
            end_royals = hostile_royals(*b, end.second);
            exposed = end_royals | (exposed & ~b->occupied());
        }

        auto &aggregate = end.second == player ? result.first : result.second;
        aggregate[royal_safety_data::T_PLUS] += std::popcount(exposed);
        aggregate[royal_safety_data::T_PLUS_HISTORICAL]
            += std::popcount(exposed & ~end_royals);
    }
    return result;
}

exposure_pair_t count_l_related_exposure(const state &s, bool player)
{
    using direction_t = royal_safety_data::exposure_direction;
    static constexpr std::array<exposure_direction, 6> directions{{
        { 1,  0, direction_t::L_PLUS},
        {-1,  0, direction_t::L_MINUS},
        { 1,  2, direction_t::L_PLUS_T_PLUS},
        { 1, -2, direction_t::L_PLUS_T_MINUS},
        {-1,  2, direction_t::L_MINUS_T_PLUS},
        {-1, -2, direction_t::L_MINUS_T_MINUS},
    }};

    exposure_pair_t result{};

    const auto [l_min, l_max] = s.get_lines_range();

    std::vector<exposure_board> boards;
    std::map<board_coordinate, std::size_t> board_indices;
    std::map<int, int> timeline_start;
    int v_min = std::numeric_limits<int>::max();
    int v_max = std::numeric_limits<int>::min();

    for(int l = l_min; l <= l_max; ++l)
    {
        const auto start = s.get_timeline_start(l);
        const auto end = s.get_timeline_end(l);
        const int start_v = turn_index(start.first, start.second);
        const int end_v = turn_index(end.first, end.second);
        timeline_start.emplace(l, start_v);
        v_min = std::min(v_min, start_v);
        v_max = std::max(v_max, end_v);
        for(int v = start_v; v <= end_v; ++v)
        {
            const auto [t, c] = index_turn(v);
            const board_coordinate coordinate{l, v};
            board_indices.emplace(coordinate, boards.size());
            boards.push_back({
                coordinate,
                s.get_board(l, t, c)
            });
        }
    }

    const auto is_established_vacuum = [&](board_coordinate coordinate)
    {
        const auto it = timeline_start.find(coordinate.l);
        return it != timeline_start.end() && it->second > coordinate.v;
    };

    for(const exposure_direction &direction : directions)
    {
        std::vector<std::size_t> order(boards.size());
        for(std::size_t i = 0; i < order.size(); ++i)
        {
            order[i] = i;
        }
        std::ranges::sort(order, [&](std::size_t a, std::size_t b)
        {
            const board_coordinate qa = boards[a].coordinate;
            const board_coordinate qb = boards[b].coordinate;
            const long long pa = static_cast<long long>(qa.l) * direction.dl
                               + static_cast<long long>(qa.v) * direction.dv;
            const long long pb = static_cast<long long>(qb.l) * direction.dl
                               + static_cast<long long>(qb.v) * direction.dv;
            return std::tie(pa, qa.l, qa.v) < std::tie(pb, qb.l, qb.v);
        });

        std::vector<bitboard_t> exposed(boards.size());
        for(std::size_t index : order)
        {
            const exposure_board &current = boards[index];
            const bool attacker = static_cast<bool>(current.coordinate.v & 1);
            bitboard_t inherited = 0;
            board_coordinate predecessor{
                current.coordinate.l - direction.dl,
                current.coordinate.v - direction.dv
            };

            // Provisional gaps are transparent: a future extension may fill
            // them.  An established vacuum terminates the sliding ray.
            while(predecessor.l >= l_min && predecessor.l <= l_max
                  && predecessor.v >= v_min && predecessor.v <= v_max)
            {
                if(const auto it = board_indices.find(predecessor);
                   it != board_indices.end())
                {
                    inherited = exposed[it->second];
                    break;
                }
                if(is_established_vacuum(predecessor))
                {
                    break;
                }
                predecessor.l -= direction.dl;
                predecessor.v -= direction.dv;
            }

            const bitboard_t local_royals
                = hostile_royals(*current.value, attacker);
            exposed[index] = local_royals
                           | (inherited & ~current.value->occupied());

            auto &aggregate = attacker == player ? result.first : result.second;
            const auto feature = orient_l_direction(direction.feature, player);
            aggregate[feature] += std::popcount(exposed[index]);
        }
    }
    return result;
}

std::pair<int, int> count_checks(const state &s, bool attacker)
{
    int checks = 0;
    int strong_checks = 0;
    std::map<std::tuple<int, int, bool>, std::set<int>> historical_sources;
    for(const full_move &check : s.find_checks(attacker))
    {
        ++checks;
        const auto target_end = s.get_timeline_end(check.to.l());
        if(target_end == turn_t(check.to.t(), attacker))
        {
            continue;
        }
        historical_sources[{check.from.l(), check.from.t(), attacker}]
            .insert(check.from.xy());
    }
    for(const auto &[source_board, pieces] : historical_sources)
    {
        (void)source_board;
        if(pieces.size() >= 2)
        {
            ++strong_checks;
        }
    }
    return {checks, strong_checks};
}
}

royal_safety_data count_royal_safety(const state &s)
{
    royal_safety_data result;
    const bool player = s.get_present().second;

    const auto [friendly_t, hostile_t] = count_t_plus_exposure(s, player);
    const auto [friendly_l, hostile_l] = count_l_related_exposure(s, player);
    for(std::size_t i = 0; i < royal_safety_data::EXPOSURE_COUNT; ++i)
    {
        result.friendly_exposure[i] = friendly_t[i] + friendly_l[i];
        result.hostile_exposure[i] = hostile_t[i] + hostile_l[i];
    }

    std::tie(result.friendly_checks, result.friendly_strong_checks)
        = count_checks(s, player);
    std::tie(result.hostile_checks, result.hostile_strong_checks)
        = count_checks(s, !player);
    return result;
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
