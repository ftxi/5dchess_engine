#include "mcts.h"
#include <limits>
#include <cmath>
#include <chrono>
#include <stop_token>
#include "hypercuboid.h"

#include "scope.h"

constexpr float INF = std::numeric_limits<float>::infinity();
constexpr int ROLLOUT_MAX_ACTIONS = 200;
constexpr int DEPTH_TO_ITERATION_MULTIPLIER = 10; // if depth limit is set, iteration_limit = depth_limit * DEPTH_TO_ITERATION_MULTIPLIER

namespace
{

using node_t = fine_node<mcts_node_info>;

node_t *expand(node_t *node, std::stop_token stop_token)
{
    if(node->get_info().fully_expanded)
    {
        return nullptr;
    }
    // mark unexpanded children as included if possible
    if(!node->get_info().all_children_included)
    {
        for(node_t *child : node->get_children())
        {
            if(!child->get_info().is_included)
            {
                child->get_info().is_included = true;
                return child;
            }
        }
        node->get_info().all_children_included = true;
        // otherwise, fall through
    }
    if(stop_token.stop_requested())
    {
        return nullptr;
    }
    if(node->is_ceiling() && !node->is_nodal())
    {
        node->ignite();
        // after ignition, the player flips to the opponent for deeper expansion
        node->get_info().player = node->get_context()->hc_info.s.get_present().second;
    }
    // search out another branch
    if(auto i_opt = node->search().first())
    {
        node_t *child = node->get_child(*i_opt);
        assert(child != nullptr);
        return child;
    }
    node->get_info().fully_expanded = true;
    return nullptr;
}

node_t *best_child(node_t *node)
{
    node_t *best_child = nullptr;
    bool max_player = node->get_info().player; // white=max, black=min
    float best_val = max_player ? -INF : INF;
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
    return best_child;
}


node_t *tree_policy(node_t *node, std::stop_token stop_token)
{
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
    node_t *ceiling_node = node->get_nearby_ceiling();
    if(!ceiling_node)
    {
        return {0.0f, 0, true, true};
    }
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
            bool ok = true;
            for(full_move fm : *mvs)
            {
                if(!s.apply_move(fm))
                {
                    ok = false;
                    break;
                }
            }
            if(ok && !s.submit())
            {
                ok = false;
            }
            if(!ok)
            {
                return {0.0f, num_actions, true, true};
            }
        }
        else
        {
            float outcome = player ? -INF : INF;
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
        info.visits++;
        info.sum_reward += outcome;
        node = node->get_parent();
    }
}


} // anonymous namespace


float uct(float sum_reward, std::size_t visits, std::size_t parent_visits)
{
    if(visits == 0)
    {
        return INF;
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
    if(!get_current_state().has_value())
    {
        return std::nullopt;
    }
    root = fine_node<mcts_node_info>::make_root(*get_current_state());
    root->get_info().player = get_current_state()->get_present().second;

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
            break;
        }
        if(deadline.has_value() && std::chrono::steady_clock::now() >= deadline.value())
        {
            break;
        }
        node_t *node = tree_policy(root.get(), stop_token);
        if(node == nullptr)
        {
            break;
        }
        simulation_result result = default_policy(node, ROLLOUT_MAX_ACTIONS, stop_token);
        if(result.aborted)
        {
            break;
        }
        backpropagate(node, result.outcome);
        iteration_count++;
    }
    node_t *current_node = root.get();
    node_t *previous_node = nullptr;
    while(current_node && !current_node->is_ceiling())
    {
        previous_node = current_node;
        current_node = best_child(current_node);
    }
    if(!current_node && previous_node)
    {
        // best_child may return nullptr if all children have 0 visits, in which case we will use the previous node to find a nearby ceiling node
        current_node = previous_node->get_nearby_ceiling();
    }
    if(current_node)
    {
        /* if best child is found, iteratively find best decandants until a ceiling node is reached, then return the corresponding action */
        moveseq best_moves = current_node->to_action();
        std::vector<ext_move> best_ext_moves;
        for(const full_move &fm : best_moves)
        {
            best_ext_moves.emplace_back(fm);
        }
        return action::from_vector(best_ext_moves, get_current_state().value());
    }
    else
    {
        return std::nullopt;
    }
}
