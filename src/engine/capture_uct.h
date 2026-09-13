#ifndef CAPTURE_UCT_H
#define CAPTURE_UCT_H

#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "capture_ordering.h"
#include "uct.h"

struct capture_uct_node_data
{
    bool all_children_included = false;
    bool fully_expanded = false;
    std::optional<std::vector<std::vector<float>>> coordinate_scores;

    capture_uct_node_data() = default;
    capture_uct_node_data(const capture_uct_node_data &) = delete;
    capture_uct_node_data &operator=(const capture_uct_node_data &) = delete;
    capture_uct_node_data(capture_uct_node_data &&) noexcept = default;
    capture_uct_node_data &operator=(capture_uct_node_data &&) noexcept = default;
};

class capture_uct_tree_policy
{
public:
    using node_data = capture_uct_node_data;
    using node_t = tree_node_t<capture_uct_tree_policy>;

private:
    std::optional<std::mt19937> rng;
    std::mt19937 *rng_pointer() { return rng ? &*rng : nullptr; }
    std::mt19937 &random_engine()
    {
        if(rng) return *rng;
        static thread_local std::mt19937 fallback(std::random_device{}());
        return fallback;
    }

    node_t *action_root(node_t *node)
    {
        while(node->get_parent() && !node->is_nodal()) node = node->get_parent();
        return node;
    }

    const std::vector<std::vector<float>> *scores(
        node_t *node, std::stop_token stop)
    {
        auto *root = action_root(node);
        auto &cached = root->get_info().tree_policy_data.coordinate_scores;
        if(!cached)
        {
            auto built = capture_coordinate_scores(
                root->get_context()->hc_info, stop);
            if(!built) return nullptr;
            cached = std::move(*built);
        }
        return &*cached;
    }

    node_t *highest_scored_child(node_t *node, bool unregistered_only, std::stop_token stop)
    {
        node_t *best = nullptr;
        float best_score = -std::numeric_limits<float>::infinity();
        auto children = node->get_children();
        if(children.empty()) return nullptr;
        const auto* table = scores(node,stop);
        if(!table) return nullptr;
        random_HC_ordering::shuffle(children, rng_pointer());
        for(auto *child : children)
        {
            if(unregistered_only && child->get_info().registered) continue;
            const float score = (*table)[child->get_n()][child->get_i()];
            if(!best || score > best_score)
            {
                best = child;
                best_score = score;
            }
        }
        return best;
    }

    std::optional<index_t> scored_search(
        node_t *node, std::stop_token stop)
    {
        const auto *coordinate_scores = scores(node, stop);
        if(!coordinate_scores) return std::nullopt;
        return node->search(scored_HC_ordering(
            node->get_context()->hc_info.universe,
            *coordinate_scores, random_engine())).first();
    }

    node_t *expand(node_t *node, std::stop_token stop)
    {
        auto &data = node->get_info().tree_policy_data;
        if(data.fully_expanded || stop.stop_requested()) return nullptr;
        if(!data.all_children_included)
        {
            if(auto *child = highest_scored_child(node, true, stop))
            {
                // Completion may already have accumulated visits here.
                child->get_info().registered = true;
                return child;
            }
            data.all_children_included = true;
        }
        if(node->is_ceiling() && !node->is_nodal()) node->ignite();
        if(stop.stop_requested()) return nullptr;
        if(auto index = scored_search(node, stop))
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
    explicit capture_uct_tree_policy(
        std::optional<std::uint32_t> seed = std::nullopt)
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
                if(!scored_search(node, stop)) return nullptr;
                node->get_info().tree_policy_data.all_children_included = false;
            }
            node = highest_scored_child(node, false, stop);
        }
        return nullptr;
    }
};

#endif /* CAPTURE_UCT_H */
