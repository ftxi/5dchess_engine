#include "mcts.h"
#include <limits>
#include <cmath>
#include <chrono>
#include <stop_token>
#include "hypercuboid.h"
#include "scope.h"
#include "utils.h"

//#define DEBUGMSG
#include "debug.h"

constexpr float WINNING_SCORE = 100000.0f;
constexpr int ROLLOUT_MAX_ACTIONS = 200;
constexpr int DEPTH_TO_ITERATION_MULTIPLIER = 10; // if depth limit is set, iteration_limit = depth_limit * DEPTH_TO_ITERATION_MULTIPLIER

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
        // after ignition, the player flips to the opponent for deeper expansion
        node->get_info().player = node->get_context()->hc_info.s.get_present().second;
    }
    // search out another branch
    dprint("expand: calling search()");
    if(auto i_opt = node->search().first())
    {
        node_t *child = node->get_child(*i_opt);
        assert(child != nullptr);
        // Reset child info to defaults so it doesn't inherit the parent's accumulated
        // sum_reward, visits, and tracking flags (all_children_included, is_included, etc.)
        child->set_info(mcts_node_info{});
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
    bool max_player = !node->get_info().player; // white=max, black=min
    float best_val = max_player ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    for(node_t *child : node->get_children())
    {
        const auto &info = child->get_info();
        if(info.visits == 0)
        {
            continue;
        }
        float uct_score = uct(info.sum_reward, info.visits, node->get_info().visits);
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

struct simulation_result
{
    float outcome;
    int actions;
    bool limit_reached;
    bool aborted;
};

simulation_result default_policy(node_t *node, int max_actions, std::stop_token stop_token)
{
    dprint("default_policy()", node->print_semimove(), "max_actions=", max_actions);
    if(node->is_terminal())
    {
        const state &s = node->get_context()->hc_info.s;
        float outcome = s.get_mate_type() == state::mate_type::STALEMATE
            ? 0.0f
            : (s.get_present().second ? WINNING_SCORE : -WINNING_SCORE);
        dprint("default_policy: current node is already terminal, returning outcome=", outcome);
        return {outcome, 0, false, false};
    }

    node_t *ceiling_node = node->get_nearby_ceiling();
    assert(ceiling_node != nullptr && "default_policy: noneterminal node should have a nearby ceiling node");
    
    if(!ceiling_node->is_nodal())
    {
        ceiling_node->ignite();
    }
    state s = ceiling_node->get_context()->hc_info.s;
    int num_actions;
    for(num_actions = 0; num_actions < max_actions; num_actions++)
    {
        if(stop_token.stop_requested())
        {
            return {0.0f, num_actions, true, true};
        }
        [[maybe_unused]] auto [present, player] = s.get_present();
        auto [w, ss] = HC_info::build_HC(s);
        w.shuffle(ss);
        if(auto mvs = w.iterative_search(ss).first())
        {
            for(full_move fm : *mvs)
            {
                s.apply_move(fm);
            }
            s.submit();
        }
        else
        {
            float outcome = s.get_mate_type() == state::mate_type::STALEMATE
                ? 0.0f
                : (player ? WINNING_SCORE : -WINNING_SCORE);
            return {outcome, num_actions, false, false};
        }
    }
    return {0.0f, num_actions, true, false};
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


} // anonymous namespace


float uct(float sum_reward, std::size_t visits, std::size_t parent_visits)
{
    if(visits == 0)
    {
        return std::numeric_limits<float>::infinity();
    }

    const float average_reward = sum_reward / static_cast<float>(visits);
    const float logParent = std::logf(static_cast<float>(parent_visits) + 1.0f);
    const float exploration = exploration_constant * std::sqrtf(logParent / static_cast<float>(visits));
    return average_reward + exploration;
}

void mcts_engine::initialize()
{
    root = nullptr;
}

std::optional<action> mcts_engine::find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token)
{
    dprint("find_best_move()",
           "depth_limit=", (depth_limit.has_value() ? std::to_string(*depth_limit) : "none"),
           "time_limit_ms=", (time_limit_ms.has_value() ? std::to_string(*time_limit_ms) : "none"));
    root = fine_node<mcts_node_info>::make_root(*get_current_state());
    root->get_info().player = get_current_state()->get_present().second;
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
        simulation_result result = default_policy(node, ROLLOUT_MAX_ACTIONS, stop_token);
        if(result.aborted)
        {
            dprint("find_best_move: simulation aborted at iteration", iteration_count,
                   "actions=", result.actions,
                   "limit_reached=", result.limit_reached);
            break;
        }
        backpropagate(node, result.outcome);
        iteration_count++;
    }
    dprint("find_best_move: post-loop, iterations=", iteration_count,
           "root_visits=", root->get_info().visits,
           "root_children=", root->get_children().size());
    node_t *current_node = root.get();
    node_t *previous_node = nullptr;
    while(current_node && !current_node->is_ceiling())
    {
        previous_node = current_node;
        current_node = best_child(current_node);
        if(current_node)
        {
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
                   show_semimove(hc_info.axis_coords[i][pt[i]]));
        }
#endif
        std::vector<ext_move> best_ext_moves;
        for(const full_move &fm : best_moves)
        {
            best_ext_moves.emplace_back(fm);
        }
        return action::from_vector(best_ext_moves, get_current_state().value());
    }
    else
    {
        dprint("find_best_move: returning nullopt",
               "iterations=", iteration_count,
               "root_visits=", root->get_info().visits,
               "root_children=", root->get_children().size());
        return std::nullopt;
    }
}
