#ifndef UCT_H
#define UCT_H

#include <cstddef>
#include <limits>
#include "mcts.h"

constexpr float exploration_constant = 1.4142135623730951f;

// Adversarial UCT score using rewards from White's fixed perspective.
// White maximizes average_reward + exploration; Black minimizes
// average_reward - exploration.
float uct(
    float sum_reward,
    std::size_t visits,
    std::size_t parent_visits,
    bool maximizing_player
);

struct uct_node_data
{
    bool all_children_included = false;
    bool fully_expanded = false;

    uct_node_data() = default;
    uct_node_data(const uct_node_data &) = delete;
    uct_node_data &operator=(const uct_node_data &) = delete;
    uct_node_data(uct_node_data &&) noexcept = default;
    uct_node_data &operator=(uct_node_data &&) noexcept = default;
};

class uct_tree_policy
{
public:
    using node_data = uct_node_data;
    using node_t = tree_node_t<uct_tree_policy>;

private:
    std::optional<std::mt19937> rng;
    std::mt19937 *rng_pointer() { return rng ? &*rng : nullptr; }
    random_HC_ordering ordering(node_t *node)
    {
        const auto &universe = node->get_context()->hc_info.universe;
        return rng ? random_HC_ordering(universe, *rng)
                   : random_HC_ordering(universe);
    }

    node_t *expand(node_t *node, std::stop_token stop)
    {
        auto &data = node->get_info().tree_policy_data;
        if(data.fully_expanded || stop.stop_requested()) return nullptr;
        if(!data.all_children_included)
        {
            auto children = node->get_children();
            random_HC_ordering::shuffle(children, rng_pointer());
            for(auto *child : children)
            {
                if(!child->get_info().registered)
                {
                    // Completion may already have accumulated visits here.
                    child->get_info().registered = true;
                    return child;
                }
            }
            data.all_children_included = true;
        }
        if(node->is_ceiling() && !node->is_nodal()) node->ignite();
        if(stop.stop_requested()) return nullptr;
        if(auto index = node->search(ordering(node)).first())
        {
            auto *child = node->get_child(*index);
            child->get_info().registered = true;
            // Search can also materialize sibling branches.
            data.all_children_included = false;
            return child;
        }
        data.fully_expanded = true;
        return nullptr;
    }

    node_t *best_child(node_t *node)
    {
        node_t *best = nullptr;
        const bool maximizing = !node->get_player();
        float best_score = maximizing ? -std::numeric_limits<float>::infinity()
                                      : std::numeric_limits<float>::infinity();
        for(auto *child : node->get_children())
        {
            const auto &info = child->get_info();
            if(!info.registered || info.visits == 0) continue;
            const float score = uct(info.sum_reward, info.visits,
                                    node->get_info().visits, maximizing);
            if(!best || (maximizing ? score > best_score : score < best_score))
            {
                best = child;
                best_score = score;
            }
        }
        return best;
    }

public:
    explicit uct_tree_policy(std::optional<std::uint32_t> seed = std::nullopt)
    {
        if(seed) rng.emplace(*seed);
    }

    template<class Observer>
    node_t *select(node_t *node, std::stop_token stop, Observer &)
    {
        if(node) node->get_info().registered = true;
        while(node && !stop.stop_requested())
        {
            if(auto *child = expand(node, stop)) return child;
            if(stop.stop_requested()) return nullptr;
            auto *child = best_child(node);
            if(!child) return node;
            node = child;
        }
        return nullptr;
    }

    template<class Observer>
    node_t *complete_to_ceiling(node_t *node, std::stop_token stop, Observer &)
    {
        while(node && !stop.stop_requested())
        {
            if(node->is_ceiling()) return node;
            auto children = node->get_children();
            if(children.empty())
            {
                if(!node->search(ordering(node)).first()) return nullptr;
                node->get_info().tree_policy_data.all_children_included = false;
                children = node->get_children();
            }
            random_HC_ordering::shuffle(children, rng_pointer());
            node = children.front();
        }
        return nullptr;
    }
};

#endif // UCT_H
