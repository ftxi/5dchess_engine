#include "rollout.h"

#include <utility>

#include "hypercuboid.h"

namespace
{

template<HC_ordering Order>
generator<moveseq> iterative_search(
    const HC_info &hc_info,
    search_space search_space,
    Order order,
    std::stop_token stop_token)
{
    while(!search_space.empty())
    {
        if(stop_token.stop_requested())
        {
            co_return;
        }

        HC hc = search_space.back();
        search_space.pop_back();
        auto point = hc_info.take_point(hc, order);
        if(!point)
        {
            continue;
        }

        if(auto problem = hc_info.find_problem(*point, hc))
        {
            search_space.concat(hc.remove_slice(*problem));
        }
        else
        {
            co_yield hc_info.to_action(*point);
            search_space.concat(hc.remove_point(*point));
        }
    }
}

} /* anonymous namespace */

rollout_result rollout_inplace_detailed(
    state &s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    std::size_t actions = 0;
    for(int num_actions = 0; num_actions < max_actions; ++num_actions)
    {
        if(stop_token.stop_requested())
        {
            return {rollout_termination::STOPPED, std::nullopt, actions};
        }

        const auto [present, player] = s.get_present();
        (void)present;
        auto [hc_info, search_space] = HC_info::build_HC(s);
        random_HC_ordering order = rng != nullptr
            ? random_HC_ordering(hc_info.universe, *rng)
            : random_HC_ordering(hc_info.universe);
        if(auto moves = iterative_search(
               hc_info,
               std::move(search_space),
               std::move(order),
               stop_token).first())
        {
            for(const full_move &move : *moves)
            {
                s.apply_move(move);
            }
            s.submit();
            ++actions;
            continue;
        }

        if(stop_token.stop_requested())
        {
            return {rollout_termination::STOPPED, std::nullopt, actions};
        }

        if(s.get_mate_type() == state::mate_type::STALEMATE)
        {
            return {rollout_termination::STALEMATE, std::nullopt, actions};
        }
        return {
            rollout_termination::WINNER,
            std::optional<bool>{!player},
            actions
        };
    }
    return {rollout_termination::ACTION_LIMIT, std::nullopt, actions};
}

rollout_result rollout_detailed(
    state s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    return rollout_inplace_detailed(s, max_actions, stop_token, rng);
}

std::optional<bool> rollout_inplace(
    state &s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    return rollout_inplace_detailed(s, max_actions, stop_token, rng).winner;
}

std::optional<bool> rollout(
    state s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    return rollout_detailed(std::move(s), max_actions, stop_token, rng).winner;
}
