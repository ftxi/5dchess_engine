#include "flat_ucb.h"

#include <chrono>
#include <limits>
#include <random>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "hypercuboid.h"
#include "rollout.h"
#include "uct.h"

namespace
{
constexpr int depth_to_iteration_multiplier = 10;
constexpr float winning_score = 1.0f;
constexpr std::string_view rollout_max_actions_option = "rollout-max-actions";

struct flat_child
{
    moveseq moves;
    state position;
    float sum_reward = 0.0f;
    std::size_t visits = 0;
};
} /* anonymous namespace */

void flat_ucb_engine::on_option_changed(const std::string &key, const option_value_t &value)
{
    if(key == rollout_max_actions_option)
    {
        if(const auto *max_actions = std::get_if<int>(&value))
        {
            rollout_max_actions.store(*max_actions);
        }
        return;
    }
    engine::on_option_changed(key, value);
}

std::optional<action> flat_ucb_engine::find_best_move(
    std::optional<int> depth_limit,
    std::optional<int> time_limit_ms,
    std::stop_token stop_token)
{
    const auto search_started = std::chrono::steady_clock::now();
    const state &root = get_current_state().value();
    auto [hypercuboid, search_state] = HC_info::build_HC(root);
    std::vector<flat_child> children;
    for(const moveseq &moves : hypercuboid.search(search_state))
    {
        state position = root;
        for(const full_move &move : moves)
        {
            position.apply_move(move);
        }
        position.submit();
        children.push_back({moves, std::move(position)});
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
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if(time_limit_ms.has_value())
    {
        deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(*time_limit_ms);
    }

    std::optional<std::mt19937> rollout_rng;
    if(rollout_seed.has_value())
    {
        rollout_rng.emplace(*rollout_seed);
    }
    const bool maximizing_player = !root.get_present().second;
    std::size_t total_visits = 0;
    while(!stop_token.stop_requested()
          && (!iteration_limit.has_value() || total_visits < *iteration_limit)
          && (!deadline.has_value() || std::chrono::steady_clock::now() < *deadline))
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
        const std::optional<bool> winner = rollout(
            children[selected].position,
            rollout_max_actions.load(),
            stop_token,
            rollout_rng.has_value() ? &*rollout_rng : nullptr);
        if(stop_token.stop_requested())
        {
            break;
        }
        if(winner.has_value())
        {
            children[selected].sum_reward += *winner
                ? -winning_score
                : winning_score;
        }
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
               << "flat_uct_stats elapsed_seconds=" << elapsed_seconds
               << " iterations=" << total_visits
               << " ips=" << (elapsed_seconds > 0.0
                   ? static_cast<double>(total_visits) / elapsed_seconds : 0.0);
    send_info(stats_info.str());
    std::ostringstream score_info;
    score_info << std::setprecision(9) << "flat_uct_score score=" << selected_score;
    send_info(score_info.str());
    return action::from_moveseq(best->moves, root);
}
