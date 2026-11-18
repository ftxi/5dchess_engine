#include "rollout_policy.h"
#include "hypercuboid.h"
#include "utils.h"

#include <cmath>
#include <stdexcept>

namespace
{
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
    while(!space.empty())
    {
        if(stop_token.stop_requested()) co_return;

        HC hc = std::move(space.back());
        space.pop_back();
        auto point = hc_info.take_point(hc, order);
        if(!point) continue;
        if(stop_token.stop_requested()) co_return;

        const bool iterative = num_problems < iterative_problem_limit;
        auto problem = hc_info.find_problem(
            *point, iterative ? hc : hc_info.get_universe());
        if(stop_token.stop_requested()) co_return;
        if(!problem)
        {
            co_yield hc_info.to_action(*point);
            space.concat(std::move(hc).remove_point(*point));
            continue;
        }

        ++num_problems;
        if(iterative)
        {
            space.concat(std::move(hc).remove_slice(*problem));
        }
        else
        {
            space.push_back(std::move(hc));
            space.remove_slice_globally(*problem, stop_token);
        }
    }
}
} /* anonymous namespace */

std::optional<moveseq> random_action_selection::operator()(
    const state &s, std::stop_token stop, std::mt19937 *rng) const
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    if(stop.stop_requested()) return std::nullopt;
    auto order = rng ? random_HC_ordering(info.get_universe(), *rng)
                     : random_HC_ordering(info.get_universe());
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}

std::optional<reward_t<rollout_details>> evaluate_zero_position(
    state s, std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    auto moves = mixed_search(
        info, std::move(space), natural_HC_ordering{}, stop).first();
    if(stop.stop_requested()) return std::nullopt;

    rollout_cutoff_evaluation evaluation;
    if(!moves)
    {
        std::optional<bool> winner;
        // Exhaustion already proved that no legal action exists. Only the
        // royal-safety check is needed to distinguish checkmate and stalemate.
        if(s.has_phantom_check()) winner = !s.get_present().c();
        if(stop.stop_requested()) return std::nullopt;
        return evaluation.mate_reward(winner, 0);
    }
    return evaluation.cutoff_reward(std::move(s), 0, stop, nullptr);
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
    if(stop.stop_requested()) return std::nullopt;
    const float weight_temperature = get_temperature();
    move_evaluator evaluator(info, weights);
    std::vector<std::vector<float>> coordinate_weights(info.get_universe().dimension());
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
    {
        auto &axis_weights = coordinate_weights[axis];
        axis_weights.reserve(info.get_universe()[axis].size());
        for(index_t coordinate : info.get_universe()[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            axis_weights.push_back(std::exp(
                evaluator.score(axis, coordinate) / weight_temperature));
        }
    }
    auto order = rng
        ? weighted_HC_ordering(
            info.get_universe(), std::move(coordinate_weights), *rng)
        : weighted_HC_ordering(
            info.get_universe(), std::move(coordinate_weights));
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}
