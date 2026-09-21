#ifndef CAPTURE_PW_UCT_H
#define CAPTURE_PW_UCT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

#include "capture_ordering.h"
#include "tactical_ordering.h"
#include "uct.h"

inline constexpr double default_progressive_widening_constant = 1.0;
inline constexpr double default_progressive_widening_alpha = 0.5;

using capture_score_table = std::vector<std::vector<float>>;

struct capture_ordering_cache
{
    capture_score_table scores;
    scored_HC_ordering expansion_ordering;

    capture_ordering_cache(
        capture_score_table scores_, const HC &universe, std::mt19937 &rng)
        : scores(std::move(scores_)),
          expansion_ordering(universe, scores, rng)
    {}
};

struct capture_pw_uct_node_data
{
    bool all_children_included = false;
    bool fully_expanded = false;
    std::size_t registered_children = 0;
    // Populated only on nodal action roots. The pointed-to scores and fixed
    // expansion ordering are immutable and may be shared by temporary nodes.
    std::shared_ptr<const capture_ordering_cache> ordering_cache;
};

class capture_pw_uct_tree_policy
{
public:
    using node_data = capture_pw_uct_node_data;
    using node_t = tree_node_t<capture_pw_uct_tree_policy>;

private:
    std::optional<std::mt19937> rng;
    double widening_constant;
    double widening_alpha;
    bool include_checks;

    std::mt19937 &random_engine()
    {
        if(rng) return *rng;
        static thread_local std::mt19937 fallback(std::random_device{}());
        return fallback;
    }

    static node_t *action_root(node_t *node)
    {
        while(node->get_parent() && !node->is_nodal()) node = node->get_parent();
        return node;
    }

    const capture_ordering_cache *cache(node_t *node, std::stop_token stop)
    {
        auto *root = action_root(node);
        auto &cached = root->get_info().tree_policy_data.ordering_cache;
        if(!cached)
        {
            auto scores = include_checks
                ? capture_check_coordinate_scores(
                    root->get_context()->hc_info, stop)
                : capture_coordinate_scores(
                    root->get_context()->hc_info, stop);
            if(!scores) return nullptr;
            const auto &universe = root->get_context()->hc_info.universe;
            cached = std::make_shared<const capture_ordering_cache>(
                std::move(*scores), universe, random_engine());
        }
        return cached.get();
    }

    static node_t *highest_ordered_child(
        node_t *node, const scored_HC_ordering &ordering,
        bool unregistered_only)
    {
        node_t *best = nullptr;
        std::size_t best_rank = std::numeric_limits<std::size_t>::max();
        for(auto *child : node->get_children())
        {
            if(unregistered_only && child->get_info().registered) continue;
            const std::size_t rank = ordering.rank(
                child->get_n(), child->get_i());
            if(!best || rank < best_rank)
            {
                best = child;
                best_rank = rank;
            }
        }
        return best;
    }

    static std::optional<index_t> ordered_search(
        node_t *node, const scored_HC_ordering &ordering,
        std::stop_token stop)
    {
        if(stop.stop_requested()) return std::nullopt;
        return node->search(ordering).first();
    }

    static void register_child(node_t *parent, node_t *child)
    {
        auto &child_info = child->get_info();
        if(child_info.registered) return;
        child_info.registered = true;
        ++parent->get_info().tree_policy_data.registered_children;
    }

    bool can_widen(const node_t *node) const
    {
        const auto &info = node->get_info();
        return !info.tree_policy_data.fully_expanded
            && info.tree_policy_data.registered_children
                < widening_limit(info.visits);
    }

    node_t *admit_next_child(node_t *node, std::stop_token stop)
    {
        auto &data = node->get_info().tree_policy_data;
        if(data.fully_expanded || stop.stop_requested()) return nullptr;

        if(!data.all_children_included)
        {
            if(!node->get_children().empty())
            {
                const auto *ordering_cache = cache(node, stop);
                if(!ordering_cache) return nullptr;
                if(auto *child = highest_ordered_child(
                       node, ordering_cache->expansion_ordering, true))
                {
                    register_child(node, child);
                    return child;
                }
            }
            data.all_children_included = true;
        }

        if(node->is_ceiling() && !node->is_nodal()) node->ignite();
        if(stop.stop_requested()) return nullptr;

        const auto *ordering_cache = cache(node, stop);
        if(!ordering_cache) return nullptr;
        if(auto index = ordered_search(
               node, ordering_cache->expansion_ordering, stop))
        {
            auto *child = node->get_child(*index);
            register_child(node, child);
            // Searching a hypercuboid may materialize sibling branches.
            data.all_children_included = false;
            return child;
        }
        data.fully_expanded = true;
        return nullptr;
    }

    static node_t *best_child(node_t *node)
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
    explicit capture_pw_uct_tree_policy(
        std::optional<std::uint32_t> seed = std::nullopt,
        double widening_constant_ = default_progressive_widening_constant,
        double widening_alpha_ = default_progressive_widening_alpha,
        bool include_checks_ = true)
        : widening_constant(widening_constant_),
          widening_alpha(widening_alpha_),
          include_checks(include_checks_)
    {
        if(!(widening_constant > 0.0) || !std::isfinite(widening_constant))
            throw std::invalid_argument("invalid progressive widening constant");
        if(!(widening_alpha > 0.0 && widening_alpha <= 1.0)
           || !std::isfinite(widening_alpha))
            throw std::invalid_argument("invalid progressive widening alpha");
        if(seed) rng.emplace(*seed);
    }

    std::size_t widening_limit(std::size_t visits) const
    {
        const double raw = std::ceil(
            widening_constant
            * std::pow(static_cast<double>(visits), widening_alpha));
        if(raw >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
            return std::numeric_limits<std::size_t>::max();
        return std::max<std::size_t>(1, static_cast<std::size_t>(raw));
    }

    template<class Observer>
    node_t *select(node_t *node, std::stop_token stop, Observer &)
    {
        if(node) node->get_info().registered = true;
        while(node && !stop.stop_requested())
        {
            if(can_widen(node))
                if(auto *child = admit_next_child(node, stop)) return child;
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
        if(!node || stop.stop_requested()) return nullptr;
        if(node->is_ceiling()) return node;
        const auto *ordering_cache = cache(node, stop);
        if(!ordering_cache) return nullptr;
        scored_HC_ordering completion_ordering(
            node->get_context()->hc_info.universe,
            ordering_cache->scores, random_engine());

        while(node && !stop.stop_requested())
        {
            if(node->is_ceiling()) return node;
            auto children = node->get_children();
            if(children.empty())
            {
                if(!ordered_search(node, completion_ordering, stop))
                    return nullptr;
                node->get_info().tree_policy_data.all_children_included = false;
            }
            node = highest_ordered_child(node, completion_ordering, false);
        }
        return nullptr;
    }
};

#endif /* CAPTURE_PW_UCT_H */
