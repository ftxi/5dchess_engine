#include "rollout_policy.h"
#include "hypercuboid.h"
#include "utils.h"
#include "search_trace.h"
#include <sstream>
#include <algorithm>
#include <type_traits>

#include <cmath>
#include <stdexcept>

namespace
{
struct legality_trace
{
    search_trace_phase phase;
    std::size_t candidates = 0, rejected = 0, initial_hcs = 0, peak_hcs = 0, remaining_hcs = 0;
    bool legal_found = false;
    double point_ms = 0, problem_ms = 0, pruning_ms = 0;
    explicit legality_trace(const state &s, std::size_t hcs)
        : phase("mixed_search", &s), initial_hcs(hcs), peak_hcs(hcs), remaining_hcs(hcs) {}
    ~legality_trace()
    {
        if(!phase.enabled()) return;
        try
        {
            std::ostringstream out;
            out << "legal_found=" << legal_found << "\ninitial_hcs=" << initial_hcs << "\ncandidates=" << candidates << "\nrejected=" << rejected
                << "\npeak_hcs=" << peak_hcs << "\nremaining_hcs=" << remaining_hcs
                << "\ntake_point_ms=" << point_ms << "\nfind_problem_ms=" << problem_ms
                << "\npruning_ms=" << pruning_ms;
            phase.set_details(out.str());
        }
        catch(...) {}
    }
    template<class F>
    auto measure(double &total, F &&work)
    {
        if(!phase.enabled()) return work();
        const auto start = std::chrono::steady_clock::now();
        if constexpr(std::is_void_v<decltype(work())>)
        {
            work();
            total += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
        else
        {
            auto result = work();
            total += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return result;
        }
    }
    void observe(std::size_t hcs)
    {
        remaining_hcs = hcs;
        peak_hcs = std::max(peak_hcs, hcs);
    }
};

template<HCOrdering Order>
generator<moveseq> mixed_search(
    const HC_info &hc_info,
    search_space space,
    Order order,
    std::stop_token stop_token
)
{
    legality_trace trace(hc_info.get_state(), space.size());
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
        trace.observe(space.size());
        ++trace.candidates;
        auto point = trace.measure(trace.point_ms, [&] { return hc_info.take_point(hc, order); });
        if(!point)
        {
            continue;
        }

        auto problem = trace.measure(trace.problem_ms, [&] { return hc_info.find_problem(*point, hc); });
        if(problem)
        {
            ++num_problems;
            ++trace.rejected;
            trace.measure(trace.pruning_ms, [&] { space.concat(hc.remove_slice(*problem)); });
            trace.observe(space.size());
        }
        else
        {
            trace.legal_found = true;
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
        trace.observe(space.size());
        ++trace.candidates;
        auto point = trace.measure(trace.point_ms, [&] { return hc_info.take_point(hc, order); });
        if(!point)
        {
            continue;
        }

        auto problem = trace.measure(trace.problem_ms, [&] { return hc_info.find_problem(*point, hc); });
        if(problem)
        {
            ++trace.rejected;
            trace.measure(trace.pruning_ms, [&] {
                space.push_back(std::move(hc));
                space.remove_slice_backwards(*problem, true, 10);
            });
            trace.observe(space.size());
        }
        else
        {
            trace.legal_found = true;
            co_yield hc_info.to_action(*point);
            space.concat(hc.remove_point(*point));
        }
    }
}
} /* anonymous namespace */

std::optional<moveseq> random_action_selection::operator()(
    const state &s, std::stop_token stop, std::mt19937 *rng) const
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = [&] {
        search_trace_phase phase("build_HC", &s);
        return HC_info::build_HC(s);
    }();
    auto order = rng ? random_HC_ordering(info.get_universe(), *rng)
                     : random_HC_ordering(info.get_universe());
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}

std::optional<reward_t<rollout_details>> evaluate_zero_position(
    state s, std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = [&] {
        search_trace_phase phase("build_HC", &s);
        return HC_info::build_HC(s);
    }();
    auto moves = mixed_search(
        info, std::move(space), natural_HC_ordering{}, stop).first();
    if(stop.stop_requested()) return std::nullopt;

    rollout_cutoff_evaluation evaluation;
    if(!moves)
    {
        std::optional<bool> winner;
        // Exhaustive legality search already established that no action exists.
        // Re-running get_mate_type() would repeat that expensive proof.
        search_trace_phase phase("terminal_classification", &s);
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
    auto [info, space] = [&] {
        search_trace_phase phase("build_HC", &s);
        return HC_info::build_HC(s);
    }();
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
