#include "monkey.h"
#include "hypercuboid.h"

std::optional<action> monkey_engine::find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token)
{
    (void)depth_limit;
    (void)time_limit_ms;
    const state &s = get_current_state().value();
    auto [w, ss] = HC_info::build_HC(s);
    w.shuffle(ss);
    if(stop_token.stop_requested())
    {
        return std::nullopt;
    }
    if(auto mvs = w.iterative_search(ss).first())
    {
        std::vector<ext_move> ext_mvs;
        for(full_move fm : *mvs)
        {
            ext_mvs.emplace_back(fm);
        }
        action act = action::from_vector(ext_mvs, s);
        return act;
    }
    else
    {
        return std::nullopt;
    }
}
