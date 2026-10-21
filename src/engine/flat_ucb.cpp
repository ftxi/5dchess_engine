#include "flat_ucb.h"

#include <chrono>
#include <limits>
#include <iomanip>
#include <sstream>

#include "hypercuboid.h"
#include "uct.h"
#include "search_control.h"
#include "search_trace.h"

namespace
{
constexpr int depth_to_iteration_multiplier = 10;

struct flat_child
{
    moveseq moves;
    state position;
    float sum_reward = 0.0f;
    std::size_t visits = 0;
};

struct flat_ucb_observer {};
} /* anonymous namespace */

template<class DefaultPolicy>
void basic_flat_ucb_engine<DefaultPolicy>::on_option_changed(
    const std::string &key, const option_value_t &value)
{
    const auto result = dispatch_watched_option(default_policy, key, value);
    if(result.matched != 0)
    {
        if(result.dispatched != result.matched)
        {
            send_debug_info("option type mismatch: " + key);
        }
        return;
    }
    engine::on_option_changed(key, value);
}

template<class DefaultPolicy>
std::optional<action> basic_flat_ucb_engine<DefaultPolicy>::find_best_move(
    std::optional<int> depth_limit,
    std::optional<int> time_limit_ms,
    std::stop_token external_stop)
{
    const auto search_started = std::chrono::steady_clock::now();
    const auto deadline = time_limit_ms
        ? std::optional{search_started + std::chrono::milliseconds(*time_limit_ms)}
        : std::nullopt;
    search_stop_scope control(external_stop, deadline);
    const auto stop_token = control.get_token();
    const state &root = get_current_state().value();
    search_trace trace(root, stop_token, deadline);
    if(trace.enabled()) trace.set_configuration(
        "depth_limit=" + (depth_limit ? std::to_string(*depth_limit) : "none")
        + "\nmovetime_ms=" + (time_limit_ms ? std::to_string(*time_limit_ms) : "none")
        + "\n" + search_configuration());
    auto [hypercuboid, search_state] = [&] {
        search_trace_phase phase("flat_build_HC", &root);
        return HC_info::build_HC(root);
    }();
    std::vector<flat_child> children;
    {
        search_trace_phase phase("flat_root_enumeration", &root);
        // The core generator remains non-interruptible internally. Poll between
        // yielded actions, preserving any legal fallback already discovered.
        for(const moveseq &moves : hypercuboid.search(search_state))
        {
            state position = root;
            for(const full_move &move : moves)
            {
                position.apply_move<true>(move);
            }
            position.submit<true>();
            children.push_back({moves, std::move(position)});
            if(stop_token.stop_requested()) break;
        }
    }
    if(children.empty())
    {
        return std::nullopt;
    }

    std::optional<std::size_t> iteration_limit;
    if(depth_limit.has_value())
    {
        iteration_limit = static_cast<std::size_t>(*depth_limit)
            * depth_to_iteration_multiplier;
    }
    flat_ucb_observer observer;
    const bool maximizing_player = !root.get_present().c();
    std::size_t total_visits = 0;
    while(!stop_token.stop_requested()
          && (!iteration_limit.has_value() || total_visits < *iteration_limit)
          && !control.deadline_expired())
    {
        std::size_t selected = 0;
        float best_score = maximizing_player
            ? -std::numeric_limits<float>::infinity()
            : std::numeric_limits<float>::infinity();
        for(std::size_t i = 0; i < children.size(); ++i)
        {
            const flat_child &child = children[i];
            const float score = uct(child.sum_reward, child.visits, total_visits, maximizing_player);
            if((maximizing_player && score > best_score)
               || (!maximizing_player && score < best_score))
            {
                selected = i;
                best_score = score;
            }
        }
        trace.set_iteration(total_visits);
        const auto result = [&] {
            search_trace_phase phase("evaluation", &children[selected].position);
            return default_policy.evaluate(children[selected].position, stop_token, observer);
        }();
        if(!result)
        {
            break;
        }
        children[selected].sum_reward += result->score;
        ++children[selected].visits;
        ++total_visits;
    }

    const flat_child *best = &children.front();
    for(const flat_child &child : children)
    {
        if(child.visits > best->visits)
        {
            best = &child;
        }
    }
    const float selected_score = best->visits
        ? best->sum_reward / static_cast<float>(best->visits)
        : 0.0f;
    const double elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - search_started).count();
    std::ostringstream stats_info;
    stats_info << std::setprecision(17)
               << "flat_ucb_stats elapsed_seconds=" << elapsed_seconds
               << " iterations=" << total_visits
               << " ips=" << (elapsed_seconds > 0.0
                   ? static_cast<double>(total_visits) / elapsed_seconds : 0.0);
    send_info(stats_info.str());
    std::ostringstream score_info;
    score_info << std::setprecision(9) << "flat_ucb_score score=" << selected_score;
    send_info(score_info.str());
    return action::from_moveseq(best->moves, root);
}

template class basic_flat_ucb_engine<rollout_default_policy>;
template class basic_flat_ucb_engine<weighted_rollout_default_policy>;
