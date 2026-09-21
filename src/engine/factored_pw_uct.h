#ifndef FACTORED_PW_UCT_H
#define FACTORED_PW_UCT_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stop_token>
#include <stdexcept>
#include <utility>
#include <vector>

#include "capture_ordering.h"
#include "capture_pw_uct.h"
#include "tactical_ordering.h"
#include "uct.h"

// A small fixed prior prevents the additive factor approximation from
// overwhelming evidence gathered for an exact fine-tree branch.
inline constexpr double default_factor_prior_strength = 2.0;

struct factored_coordinate_statistics
{
    float sum_reward = 0.0f;
    std::size_t visits = 0;
};

struct factored_action_statistics
{
    std::vector<std::vector<float>> tactical_scores;
    std::vector<std::vector<factored_coordinate_statistics>> coordinates;

    explicit factored_action_statistics(
        std::vector<std::vector<float>> tactical_scores_)
        : tactical_scores(std::move(tactical_scores_)),
          coordinates(tactical_scores.size())
    {
        for(std::size_t axis = 0; axis < tactical_scores.size(); ++axis)
            coordinates[axis].resize(tactical_scores[axis].size());
    }
};

// Blend only observations contributed by other occurrences of this coordinate.
// The current fine-tree child is already represented by exact_sum/exact_visits;
// subtracting it avoids counting its evidence twice.
inline float factored_blended_mean(
    float exact_sum,
    std::size_t exact_visits,
    const factored_coordinate_statistics &factor,
    double prior_strength)
{
    if(exact_visits == 0) return 0.0f;
    const float exact_mean = exact_sum / static_cast<float>(exact_visits);
    if(factor.visits <= exact_visits || !(prior_strength > 0.0))
        return exact_mean;

    const std::size_t shared_visits = factor.visits - exact_visits;
    const float shared_sum = factor.sum_reward - exact_sum;
    const float shared_mean = shared_sum / static_cast<float>(shared_visits);
    const double prior_samples = std::min<double>(prior_strength, shared_visits);
    return static_cast<float>(
        (static_cast<double>(exact_sum) + prior_samples * shared_mean)
        / (static_cast<double>(exact_visits) + prior_samples));
}

struct factored_pw_uct_node_data
{
    bool all_children_included = false;
    bool fully_expanded = false;
    std::size_t registered_children = 0;

    // Populated only on nodal action roots. Temporary nodes in different
    // prefixes consult this common table, which is the source of factor
    // sharing. HC itself continues to enforce all cross-axis constraints.
    std::shared_ptr<factored_action_statistics> action_statistics;
};

class factored_pw_uct_tree_policy
{
public:
    using node_data = factored_pw_uct_node_data;
    using node_t = tree_node_t<factored_pw_uct_tree_policy>;

private:
    std::optional<std::mt19937> rng;
    double widening_constant;
    double widening_alpha;
    double factor_prior_strength;

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

    factored_action_statistics *statistics(
        node_t *node, std::stop_token stop)
    {
        auto *root = action_root(node);
        auto &cached = root->get_info().tree_policy_data.action_statistics;
        if(!cached)
        {
            auto tactical = capture_check_coordinate_scores(
                root->get_context()->hc_info, stop);
            if(!tactical) return nullptr;
            cached = std::make_shared<factored_action_statistics>(
                std::move(*tactical));
        }
        return cached.get();
    }

    std::optional<scored_HC_ordering> ordering(
        node_t *node, std::stop_token stop)
    {
        auto *root = action_root(node);
        auto *shared = statistics(root, stop);
        if(!shared) return std::nullopt;

        const bool maximizing = !root->get_player();
        std::vector<std::vector<float>> scores = shared->tactical_scores;
        for(std::size_t axis = 0; axis < scores.size(); ++axis)
        {
            for(std::size_t coordinate = 0;
                coordinate < scores[axis].size(); ++coordinate)
            {
                // Normalize the fixed tactical tiers before adding a learned
                // value in [-1, 1]. A capture remains a one-point prior and a
                // quiet check a 0.625-point prior.
                scores[axis][coordinate] /= capture_ordering_score;
                const auto &factor = shared->coordinates[axis][coordinate];
                if(factor.visits == 0) continue;
                float learned = factor.sum_reward
                    / static_cast<float>(factor.visits);
                if(!maximizing) learned = -learned;
                const double confidence = std::min(
                    1.0,
                    static_cast<double>(factor.visits)
                        / factor_prior_strength);
                scores[axis][coordinate]
                    += static_cast<float>(confidence) * learned;
            }
        }
        return scored_HC_ordering(
            root->get_context()->hc_info.universe,
            scores,
            random_engine());
    }

    static node_t *highest_ordered_child(
        node_t *node,
        const scored_HC_ordering &ordering,
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
        node_t *node,
        const scored_HC_ordering &ordering,
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

        auto order = ordering(node, stop);
        if(!order) return nullptr;
        if(!data.all_children_included)
        {
            if(!node->get_children().empty())
            {
                if(auto *child = highest_ordered_child(node, *order, true))
                {
                    register_child(node, child);
                    return child;
                }
            }
            data.all_children_included = true;
        }

        if(node->is_ceiling() && !node->is_nodal()) node->ignite();
        if(stop.stop_requested()) return nullptr;
        // Ignition starts a new action with a different factor table.
        order = ordering(node, stop);
        if(!order) return nullptr;
        if(auto index = ordered_search(node, *order, stop))
        {
            auto *child = node->get_child(*index);
            register_child(node, child);
            data.all_children_included = false;
            return child;
        }
        data.fully_expanded = true;
        return nullptr;
    }

    float selection_score(
        node_t *parent,
        node_t *child,
        const factored_action_statistics &shared) const
    {
        const auto &info = child->get_info();
        const auto &factor = shared.coordinates[child->get_n()][child->get_i()];
        const float exploitation = factored_blended_mean(
            info.sum_reward,
            info.visits,
            factor,
            factor_prior_strength);
        const float log_parent = std::log(
            static_cast<float>(parent->get_info().visits) + 1.0f);
        const float exploration = exploration_constant * std::sqrt(
            log_parent / static_cast<float>(info.visits));
        return !parent->get_player()
            ? exploitation + exploration
            : exploitation - exploration;
    }

    node_t *best_child(node_t *node, std::stop_token stop)
    {
        auto *shared = statistics(node, stop);
        if(!shared) return nullptr;
        node_t *best = nullptr;
        const bool maximizing = !node->get_player();
        float best_score = maximizing ? -std::numeric_limits<float>::infinity()
                                      : std::numeric_limits<float>::infinity();
        for(auto *child : node->get_children())
        {
            const auto &info = child->get_info();
            if(!info.registered || info.visits == 0) continue;
            const float score = selection_score(node, child, *shared);
            if(!best || (maximizing ? score > best_score : score < best_score))
            {
                best = child;
                best_score = score;
            }
        }
        return best;
    }

public:
    explicit factored_pw_uct_tree_policy(
        std::optional<std::uint32_t> seed = std::nullopt,
        double widening_constant_ = default_progressive_widening_constant,
        double widening_alpha_ = default_progressive_widening_alpha,
        double factor_prior_strength_ = default_factor_prior_strength)
        : widening_constant(widening_constant_),
          widening_alpha(widening_alpha_),
          factor_prior_strength(factor_prior_strength_)
    {
        if(!(widening_constant > 0.0) || !std::isfinite(widening_constant))
            throw std::invalid_argument("invalid progressive widening constant");
        if(!(widening_alpha > 0.0 && widening_alpha <= 1.0)
           || !std::isfinite(widening_alpha))
            throw std::invalid_argument("invalid progressive widening alpha");
        if(!(factor_prior_strength > 0.0)
           || !std::isfinite(factor_prior_strength))
            throw std::invalid_argument("invalid factor prior strength");
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

    // Exposed for focused policy tests and experiment telemetry.
    factored_action_statistics *action_statistics(
        node_t *node, std::stop_token stop = {})
    {
        return statistics(node, stop);
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
            auto *child = best_child(node, stop);
            if(!child) return node;
            node = child;
        }
        return nullptr;
    }

    template<class Observer>
    node_t *complete_to_ceiling(
        node_t *node, std::stop_token stop, Observer &)
    {
        if(!node || stop.stop_requested()) return nullptr;
        if(node->is_ceiling()) return node;
        auto order = ordering(node, stop);
        if(!order) return nullptr;

        while(node && !stop.stop_requested())
        {
            if(node->is_ceiling()) return node;
            auto children = node->get_children();
            if(children.empty())
            {
                if(!ordered_search(node, *order, stop)) return nullptr;
                node->get_info().tree_policy_data.all_children_included = false;
            }
            node = highest_ordered_child(node, *order, false);
        }
        return nullptr;
    }
};

// Normal MCTS statistics stay attached to exact fine-tree nodes. In addition,
// every selected coordinate updates the table owned by its action root. This
// shares evidence between the same coordinate reached through other prefixes.
struct factored_sum_backpropagation
{
    template<class Node, class Result, class Observer>
    void backpropagate(Node *node, const Result &reward, Observer &) const
    {
        for(; node; node = node->get_parent())
        {
            node->get_info().sum_reward += reward.score;
            ++node->get_info().visits;

            if(!node->get_parent()) continue;
            Node *root = node->get_parent();
            while(root->get_parent() && !root->is_nodal())
                root = root->get_parent();
            auto shared = root->get_info().tree_policy_data.action_statistics;
            if(!shared) continue;
            assert(node->get_n() < shared->coordinates.size());
            assert(node->get_i() < shared->coordinates[node->get_n()].size());
            auto &factor = shared->coordinates[node->get_n()][node->get_i()];
            factor.sum_reward += reward.score;
            ++factor.visits;
        }
    }
};

#endif /* FACTORED_PW_UCT_H */
