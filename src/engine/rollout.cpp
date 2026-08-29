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
    std::stop_token stop_token
)
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

rollout_result rollout_inplace(
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

rollout_result rollout(
    state s,
    int max_actions,
    std::stop_token stop_token,
    std::mt19937 *rng
)
{
    return rollout_inplace(s, max_actions, stop_token, rng);
}
