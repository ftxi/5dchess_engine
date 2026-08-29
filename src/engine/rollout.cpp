#include "rollout.h"

#include <optional>
#include <utility>

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

template<HC_ordering Order>
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

rollout_result rollout_inplace_impl(
    state &s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng
)
{
    std::size_t num_actions = 0;
    for(int action_index = 0; action_index < max_actions; ++action_index)
    {
        if(stop_token.stop_requested())
        {
            return {rollout_result::termination::STOPPED, num_actions};
        }

        const auto [present, player] = s.get_present();
        (void)present;
        auto [hc_info, search_space] = HC_info::build_HC(s);
        random_HC_ordering order = rng != nullptr
            ? random_HC_ordering(hc_info.universe, *rng)
            : random_HC_ordering(hc_info.universe);
        std::optional<moveseq> moves = mixed_search(
            hc_info,
            std::move(search_space),
            std::move(order),
            stop_token).first();
        if(moves)
        {
            for(const full_move &move : *moves)
            {
                s.apply_move(move);
            }
            s.submit();
            ++num_actions;
            continue;
        }

        if(stop_token.stop_requested())
        {
            return {rollout_result::termination::STOPPED, num_actions};
        }

        if(s.get_mate_type() == mate_type::STALEMATE)
        {
            return {rollout_result::termination::STALEMATE, num_actions};
        }
        return {
            player
                ? rollout_result::termination::WHITE_WINS
                : rollout_result::termination::BLACK_WINS,
            num_actions
        };
    }
    return {rollout_result::termination::ACTION_LIMIT, num_actions};
}

} /* anonymous namespace */

rollout_result rollout_inplace(
    state &s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng
)
{
    return rollout_inplace_impl(
        s, max_actions, stop_token, rng);
}

rollout_result rollout(
    state s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng
)
{
    return rollout_inplace(s, max_actions, stop_token, rng);
}
