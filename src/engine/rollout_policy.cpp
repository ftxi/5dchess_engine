#include "rollout_policy.h"
#include "hypercuboid.h"
#include "utils.h"

#include <cmath>
#include <stdexcept>

namespace
{
bool propagate_problem_adaptively(
    search_space &space,
    const HC &selected_hc,
    const slice &problem,
    std::stop_token stop_token
)
{
    search_space adjoined;
    adjoined.concat(selected_hc.remove_slice(problem));
    std::size_t intersect_count = 1;
    std::size_t disjoint_count = 0;
    while(!space.empty() && disjoint_count * 10 < intersect_count)
    {
        if(stop_token.stop_requested())
        {
            return false;
        }

        HC other_hc = std::move(space.back());
        space.pop_back();
        if(other_hc.intersects(problem))
        {
            adjoined.concat(other_hc.remove_slice_if_good(problem, 1));
            ++intersect_count;
        }
        else
        {
            adjoined.push_back(std::move(other_hc));
            ++disjoint_count;
        }
    }
    space.concat(std::move(adjoined));
    return true;
}

template<HCOrdering Order>
generator<moveseq> mixed_search(
    const HC_info &hc_info,
    search_space space,
    Order order,
    std::stop_token stop_token
)
{
    constexpr std::size_t iterative_problem_limit = 100;
    std::size_t num_problems = 0;
    while(!space.empty() && num_problems < iterative_problem_limit)
    {
        if(stop_token.stop_requested())
        {
            co_return;
        }

        HC hc = std::move(space.back());
        space.pop_back();
        auto point = hc_info.take_point(hc, order);
        if(!point)
        {
            continue;
        }

        if(auto problem = hc_info.find_problem(*point, hc))
        {
            ++num_problems;
            space.concat(hc.remove_slice(*problem));
        }
        else
        {
            co_yield hc_info.to_action(*point);
            space.concat(hc.remove_point(*point));
        }
    }

    while(!space.empty())
    {
        if(stop_token.stop_requested())
        {
            co_return;
        }

        HC hc = std::move(space.back());
        space.pop_back();
        auto point = hc_info.take_point(hc, order);
        if(!point)
        {
            continue;
        }

        if(auto problem = hc_info.find_problem(*point, hc))
        {
            if(!propagate_problem_adaptively(
                    space, hc, *problem, stop_token))
            {
                co_return;
            }
        }
        else
        {
            co_yield hc_info.to_action(*point);
            space.concat(hc.remove_point(*point));
        }
    }
}

}

std::optional<moveseq> random_action_selection::operator()(
    const state &s, std::stop_token stop, std::mt19937 *rng) const
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    auto order = rng ? random_HC_ordering(info.universe, *rng)
                     : random_HC_ordering(info.universe);
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}

void weighted_action_selection::set_temperature(float value)
{
    if(!(value > 0.0f) || !std::isfinite(value))
    {
        throw std::invalid_argument(
            "rollout weighting temperature must be positive and finite");
    }
    temperature->store(value);
}

std::optional<moveseq> weighted_action_selection::operator()(
    const state &s, std::stop_token stop, std::mt19937 *rng) const
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    const float weight_temperature = get_temperature();
    std::vector<std::vector<float>> coordinate_weights(info.universe.dimension());
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
    {
        auto &axis_weights = coordinate_weights[axis];
        axis_weights.reserve(info.universe[axis].size());
        for(index_t coordinate : info.universe[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            const semimove move = info.get_semimove(axis, coordinate);
            axis_weights.push_back(move.visit(overloads{
                [&](const physical_move &physical) {
                    return move_info_weight(
                        s.get_move_info(physical.m), weights, weight_temperature);
                },
                [&](const arriving_move &arriving) {
                    // An arrival retains the complete superphysical move. Its
                    // paired departure stays neutral so the move is weighted
                    // exactly once.
                    return move_info_weight(
                        s.get_move_info(arriving.m), weights, weight_temperature);
                },
                [](const departing_move &) { return 1.0f; },
                [](const null_move &) { return 1.0f; },
            }));
        }
    }
    auto order = rng
        ? weighted_HC_ordering(
            info.universe, std::move(coordinate_weights), *rng)
        : weighted_HC_ordering(
            info.universe, std::move(coordinate_weights));
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}
