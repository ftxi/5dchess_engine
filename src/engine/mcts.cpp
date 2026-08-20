#include "mcts.h"
#include "rollout.h"
#include <limits>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <stop_token>
#include <random>
#include <sstream>
#include <string_view>
#include "hypercuboid.h"
#include "scope.h"
#include "utils.h"

//#define DEBUGMSG
#include "debug.h"

constexpr int DEPTH_TO_ITERATION_MULTIPLIER = 10; // if depth limit is set, iteration_limit = depth_limit * DEPTH_TO_ITERATION_MULTIPLIER
constexpr std::string_view ROLLOUT_MAX_ACTIONS_OPTION = "rollout-max-actions";

namespace
{

using node_t = fine_node<mcts_node_info>;

node_t *expand(node_t *node, std::stop_token stop_token)
{
    dprint("expand", node->print_semimove(), (node->is_nodal() ? "nodal" : "temporary"), (node->is_ceiling() ? "ceiling" : ""),
           "fully_expanded=", node->get_info().fully_expanded,
           "all_children_included=", node->get_info().all_children_included,
           "num_children=", node->get_children().size());
    if(node->get_info().fully_expanded)
    {
        dprint("expand: fully_expanded, returning nullptr");
        return nullptr;
    }
    // mark unexpanded children as included if possible
    if(!node->get_info().all_children_included)
    {
        for(node_t *child : node->get_children())
        {
            dprint("expand: checking child", child->print_semimove(), "included=", child->get_info().is_included, "visits=", child->get_info().visits);
            if(!child->get_info().is_included)
            {
                child->set_info(mcts_node_info{});
                child->get_info().is_included = true;
                dprint("expand: returning existing unincluded child", child->print_semimove(), "visits=", child->get_info().visits);
                return child;
            }
        }
        node->get_info().all_children_included = true;
        dprint("expand: all children already included, falling through");
        // otherwise, fall through
    }
    if(stop_token.stop_requested())
    {
        dprint("expand: stop requested, returning nullptr");
        return nullptr;
    }
    if(node->is_ceiling() && !node->is_nodal())
    {
        dprint("expand: igniting ceiling node");
        node->ignite();
    }
    // search out another branch
    dprint("expand: calling search()");
    if(auto i_opt = node->search().first())
    {
        node_t *child = node->get_child(*i_opt);
        assert(child != nullptr);
        child->set_info(mcts_node_info{});
        child->get_info().is_included = true;
        dprint("expand: search() found child", child->print_semimove(), "visits=", child->get_info().visits);
        return child;
    }
    dprint("expand: search() found nothing, marking fully_expanded=true, returning nullptr");
    node->get_info().fully_expanded = true;
    return nullptr;
}

node_t *best_child(node_t *node)
{
    dprint("best_child()", node->print_semimove(), "visits=", node->get_info().visits);
    node_t *best_child = nullptr;
    bool max_player = !node->get_player(); // white=max, black=min
    float best_val = max_player ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    for(node_t *child : node->get_children())
    {
        const auto &info = child->get_info();
        if(info.visits == 0)
        {
            continue;
        }
        float uct_score = uct(
            info.sum_reward,
            info.visits,
            node->get_info().visits,
            max_player);
        bool better = max_player ? (uct_score > best_val) : (uct_score < best_val);
        if(better)
        {
            best_val = uct_score;
            best_child = child;
        }
    }
#ifdef DEBUGMSG
    if(!best_child)
    {
        dprint("best_child: totally", node->get_children().size(), "children, none visited, returning nullptr");
    }
#endif
    return best_child;
}


node_t *tree_policy(node_t *node, std::stop_token stop_token)
{
    dprint("tree_policy()", node->print_semimove(), (node->is_nodal() ? "nodal" : "temporary"), (node->is_ceiling() ? "ceiling" : ""));
    while(!node->is_terminal() && !stop_token.stop_requested())
    {
        node_t *next_node = expand(node, stop_token);
        if(next_node)
        {
            return next_node;
        }
        // if no unexpanded children, select the best child
        node_t *bc = best_child(node);
        if(bc == nullptr)
        {
            // no valid children found, return current node
            return node;
        }
        node = bc;
    }
    return node;
}

state rollout_state(node_t *node)
{
    if(node->is_terminal())
    {
        return node->get_context()->hc_info.s;
    }
    node_t *ceiling_node = node->get_nearby_ceiling();
    assert(ceiling_node != nullptr && "rollout node should have a nearby ceiling node");
    if(!ceiling_node->is_nodal())
    {
        ceiling_node->ignite();
    }
    return ceiling_node->get_context()->hc_info.s;
}

void backpropagate(node_t *node, float outcome)
{
    while(node != nullptr)
    {
        auto &info = node->get_info();
#ifdef DEBUGMSG
        float old_sum = info.sum_reward;
#endif
        info.visits++;
        info.sum_reward += outcome;
#ifdef DEBUGMSG
        assert(!std::isnan(info.sum_reward));
        (void)old_sum;
#endif
        node = node->get_parent();
    }
}

node_t *most_visited_child(node_t *node)
{
    node_t *best = nullptr;
    for(node_t *child : node->get_children())
    {
        if(child->get_info().visits == 0)
        {
            continue;
        }
        if(best == nullptr
           || child->get_info().visits > best->get_info().visits)
        {
            best = child;
        }
    }
    return best;
}

float terminal_outcome(const state &s)
{
    const auto [present, player] = s.get_present();
    (void)present;
    return s.get_mate_type() == state::mate_type::STALEMATE
        ? 0.0f
        : (player ? WINNING_SCORE : -WINNING_SCORE);
}


} /* anonymous namespace */


void mcts_engine::initialize()
{
    root = nullptr;
}

default_policy_result mcts_engine::default_policy(
    state position,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    const rollout_result result = rollout_detailed(
        std::move(position),
        rollout_max_actions.load(),
        stop_token,
        rng);
    if(!result.winner.has_value())
    {
        return {0.0f, result.termination};
    }
    return {
        *result.winner ? -WINNING_SCORE : WINNING_SCORE,
        result.termination
    };
}

void mcts_engine::on_option_changed(const std::string &key, const option_value_t &value)
{
    if(key == ROLLOUT_MAX_ACTIONS_OPTION)
    {
        if(const auto *max_actions = std::get_if<int>(&value))
        {
            rollout_max_actions.store(*max_actions);
        }
        return;
    }
    engine::on_option_changed(key, value);
}

std::optional<action> mcts_engine::find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token)
{
    const auto search_started = std::chrono::steady_clock::now();
    dprint("find_best_move()",
           "depth_limit=", (depth_limit.has_value() ? std::to_string(*depth_limit) : "none"),
           "time_limit_ms=", (time_limit_ms.has_value() ? std::to_string(*time_limit_ms) : "none"));
    root = fine_node<mcts_node_info>::make_root(*get_current_state());
    // if(root->is_terminal())
    // {
    //     dprint("find_best_move: root is terminal, returning nullopt");
    //     return std::nullopt;
    // }

    // Convert depth_limit to iteration budget if provided
    std::optional<std::size_t> iteration_limit;
    if(depth_limit.has_value())
    {
        iteration_limit = static_cast<std::size_t>(depth_limit.value()) * DEPTH_TO_ITERATION_MULTIPLIER;
    }

    // Set deadline from time_limit_ms if provided
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if(time_limit_ms.has_value())
    {
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(time_limit_ms.value());
    }

    std::size_t iteration_count = 0;
    std::size_t conclusive_rollouts = 0;
    std::size_t inconclusive_rollouts = 0;
    std::size_t terminal_tree_evaluations = 0;
    std::optional<std::mt19937> rollout_rng;
    if(rollout_seed.has_value())
    {
        rollout_rng.emplace(*rollout_seed);
    }
    while(!stop_token.stop_requested())
    {
        if(iteration_limit.has_value() && iteration_count >= iteration_limit.value())
        {
            dprint("find_best_move: iteration limit reached", iteration_count);
            break;
        }
        if(deadline.has_value() && std::chrono::steady_clock::now() >= deadline.value())
        {
            dprint("find_best_move: time deadline reached", iteration_count);
            break;
        }
        node_t *node = tree_policy(root.get(), stop_token);
        if(node == nullptr)
        {
            dprint("find_best_move: tree_policy returned nullptr at iteration", iteration_count,
                   "root_visits=", root->get_info().visits,
                   "root_children=", root->get_children().size());
            break;
        }
        const bool terminal_leaf = node->is_terminal();
        float outcome;
        std::optional<rollout_termination> rollout_end;
        if(terminal_leaf)
        {
            outcome = terminal_outcome(node->get_context()->hc_info.s);
            ++terminal_tree_evaluations;
        }
        else
        {
            const default_policy_result result = default_policy(
                rollout_state(node),
                stop_token,
                rollout_rng.has_value() ? &*rollout_rng : nullptr);
            outcome = result.score;
            rollout_end = result.termination;
        }
        if(stop_token.stop_requested()
           || rollout_end == rollout_termination::STOPPED)
        {
            dprint("find_best_move: simulation aborted at iteration", iteration_count);
            break;
        }
        if(!terminal_leaf)
        {
            if(rollout_end == rollout_termination::WINNER
               || rollout_end == rollout_termination::STALEMATE)
            {
                ++conclusive_rollouts;
            }
            else
            {
                ++inconclusive_rollouts;
            }
        }
        backpropagate(node, outcome);
        iteration_count++;
    }
    dprint("find_best_move: post-loop, iterations=", iteration_count,
           "root_visits=", root->get_info().visits,
           "root_children=", root->get_children().size());
    const auto report_search_metrics = [&]()
    {
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - search_started).count();
        const std::size_t visits = root->get_info().visits;
        const double visits_per_second = seconds > 0.0
            ? static_cast<double>(visits) / seconds
            : 0.0;
        std::ostringstream info;
        info << std::setprecision(17)
             << "mcts_stats elapsed_seconds=" << seconds
             << " iterations=" << visits
             << " ips=" << visits_per_second
             << " conclusive_rollouts=" << conclusive_rollouts
             << " inconclusive_rollouts=" << inconclusive_rollouts
             << " terminal_tree_evaluations=" << terminal_tree_evaluations;
        send_info(info.str());
    };
    node_t *current_node = root.get();
    node_t *previous_node = nullptr;
    std::vector<float> selected_scores;
    while(current_node && !current_node->is_ceiling())
    {
        previous_node = current_node;
        current_node = most_visited_child(current_node);
        if(current_node)
        {
            const auto &info = current_node->get_info();
            selected_scores.push_back(info.visits != 0
                ? info.sum_reward / static_cast<float>(info.visits)
                : 0.0f);
            dprint("find_best_move: descend to", current_node->print_semimove(),
                   "visits=", current_node->get_info().visits);
        }
        else
        {
            dprint("find_best_move: best_child returned nullptr from", previous_node->print_semimove());
        }
    }
    if(!current_node && previous_node)
    {
        dprint("find_best_move: best_child failed, trying get_nearby_ceiling from", previous_node->print_semimove());
        current_node = previous_node->get_nearby_ceiling();
        if(current_node)
        {
            dprint("find_best_move: get_nearby_ceiling succeeded, got", current_node->print_semimove());
        }
        else
        {
            dprint("find_best_move: get_nearby_ceiling returned nullptr");
        }
    }
    if(current_node)
    {
        dprint("find_best_move: success, ceiling", current_node->print_semimove());
        /* if best child is found, iteratively find best decandants until a ceiling node is reached, then return the corresponding action */
        moveseq best_moves = current_node->to_action();
        dprint("find_best_move: best_moves.size() =", best_moves.size());
        // print the point coordinates and the semimove at each axis
        auto &hc_info = current_node->get_context()->hc_info;
        std::vector<index_t> pt(hc_info.dimension);
        auto *dbg_node = current_node;
        while(dbg_node && !dbg_node->is_nodal()) {
            pt[dbg_node->get_n()] = dbg_node->get_i();
            dbg_node = dbg_node->get_parent();
        }
        dprint("find_best_move: pt =", range_to_string(pt));
        dprint("find_best_move: dimension =", hc_info.dimension);
        dprint("find_best_move: line_to_axis size =", hc_info.line_to_axis.size());
#ifdef DEBUGMSG
        for(const auto &[l,i] : hc_info.line_to_axis) {
            dprint("  axis", i, "coord", pt[i], "type",
                   hc_info.get_semimove(i, pt[i]).lan(get_current_state()));
        }
#endif
        std::vector<ext_move> best_ext_moves;
        for(const full_move &fm : best_moves)
        {
            best_ext_moves.emplace_back(fm);
        }
        report_search_metrics();
        double score_average = 0.0;
        for(float score : selected_scores)
        {
            score_average += score;
        }
        if(!selected_scores.empty())
        {
            score_average /= static_cast<double>(selected_scores.size());
        }
        std::ostringstream score_info;
        score_info << std::setprecision(9)
                   << "mcts_score average=" << score_average << " detailed=";
        for(std::size_t i = 0; i < selected_scores.size(); ++i)
        {
            if(i != 0)
            {
                score_info << ':';
            }
            score_info << selected_scores[i];
        }
        send_info(score_info.str());
        return action::from_vector(best_ext_moves, get_current_state().value());
    }
    else
    {
        dprint("find_best_move: returning nullopt",
               "iterations=", iteration_count,
               "root_visits=", root->get_info().visits,
               "root_children=", root->get_children().size());
        report_search_metrics();
        return std::nullopt;
    }
}
