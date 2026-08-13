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

simulation_result default_policy(
    state s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng,
    float winning_score)
{
    int num_actions;
    for(num_actions = 0; num_actions < max_actions; ++num_actions)
    {
        if(stop_token.stop_requested())
        {
            return {0.0f, num_actions, true, true};
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
            continue;
        }

        if(stop_token.stop_requested())
        {
            return {0.0f, num_actions, true, true};
        }

        const float outcome = s.get_mate_type() == state::mate_type::STALEMATE
            ? 0.0f
            : (player ? winning_score : -winning_score);
        return {outcome, num_actions, false, false};
    }
    return {0.0f, num_actions, true, false};
}
