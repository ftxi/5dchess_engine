#include "rollout_policy.h"
#include "hypercuboid.h"

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
