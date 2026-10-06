#ifndef PROGRESSIVE_WIDENING_H
#define PROGRESSIVE_WIDENING_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>

#include "uct.h"

inline constexpr double default_progressive_widening_constant = 2.0;
inline constexpr double default_progressive_widening_alpha = 0.5;

template<HCOrdering Order>
struct progressive_widening_node_data
{
    bool all_children_included = false;
    bool fully_expanded = false;
    std::size_t registered_children = 0;
    // Only nodal action roots populate this. Temporary nodes use that root's
    // ordering, so all coordinates of one candidate action rank consistently.
    std::optional<Order> ordering;
    std::optional<random_HC_ordering> random_ordering;
    std::uint64_t ordering_owner = 0;
    bool ordering_injected = false;
};

template<HCOrdering Order = random_HC_ordering,
         class ChildSelection = uct_child_selection>
class progressive_widening_tree_policy
{
public:
    using node_data = progressive_widening_node_data<Order>;
    using node_t = tree_node_t<progressive_widening_tree_policy<Order, ChildSelection>>;

private:
    std::optional<std::mt19937> rng;
    std::uint64_t policy_id;
    double widening_constant;
    double widening_alpha;
    coordinate_score_function score_function;
    ChildSelection child_selection;

    template<class F>
    auto with_order(node_t *node, std::stop_token stop, F &&use)
        -> decltype(use(std::declval<const Order &>()))
    {
        using result_t = decltype(use(std::declval<const Order &>()));
        if(stop.stop_requested()) return result_t{};
        auto *root = uct_detail::action_root(node);
        auto &data = root->get_info().tree_policy_data;
        auto &cached = data.ordering;
        if constexpr(ContextConstructibleHCOrdering<Order>)
        {
            if(!cached || (!data.ordering_injected && data.ordering_owner != policy_id))
            {
                if constexpr(std::constructible_from<Order, const HC &,
                             const std::vector<std::vector<float>> &, std::mt19937 &>
                             && !std::constructible_from<Order,
                                 const HC_info &, std::mt19937 &, std::stop_token>)
                {
                    if(!score_function) cached.reset();
                }
                if(score_function || !std::constructible_from<Order, const HC &,
                       const std::vector<std::vector<float>> &, std::mt19937 &>)
                {
                    auto built = construct_hc_ordering<Order>(
                        root->get_context()->hc_info,
                        uct_detail::random_engine(rng), stop, score_function);
                    if(!built) return result_t{};
                    cached = std::move(*built);
                    data.ordering_owner = policy_id;
                }
            }
        }
        if(stop.stop_requested()) return result_t{};
        if(cached) return use(*cached);
        // An order that needs caller-provided data (such as scored_HC_ordering)
        // can be injected at the action root. Until then, use random order.
        if(!data.random_ordering || data.ordering_owner != policy_id)
        {
            data.random_ordering.emplace(
                root->get_context()->hc_info.universe,
                uct_detail::random_engine(rng));
            data.ordering_owner = policy_id;
        }
        return stop.stop_requested() ? result_t{} : use(*data.random_ordering);
    }

    static void register_child(node_t *parent, node_t *child)
    {
        auto &info = child->get_info();
        if(info.registered) return;
        info.registered = true;
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
                auto *child = with_order(node, stop, [&](const auto &order) {
                    return uct_detail::first_ordered_child(node, order, true);
                });
                if(child)
                {
                    register_child(node, child);
                    return child;
                }
                if(stop.stop_requested()) return nullptr;
            }
            data.all_children_included = true;
        }

        if(node->is_ceiling() && !node->is_nodal()) node->ignite();
        if(stop.stop_requested()) return nullptr;

        auto index = with_order(node, stop, [&](const auto &order) {
            return uct_detail::ordered_search(node, order, stop);
        });
        if(index)
        {
            auto *child = node->get_child(*index);
            register_child(node, child);
            data.all_children_included = false;
            return child;
        }
        if(!stop.stop_requested()) data.fully_expanded = true;
        return nullptr;
    }

public:
    explicit progressive_widening_tree_policy(
        std::optional<std::uint32_t> seed = std::nullopt,
        double widening_constant_ = default_progressive_widening_constant,
        double widening_alpha_ = default_progressive_widening_alpha,
        coordinate_score_function score = nullptr,
        ChildSelection select_child = {})
        : policy_id(uct_detail::next_policy_id()),
          widening_constant(widening_constant_),
          widening_alpha(widening_alpha_),
          score_function(score),
          child_selection(std::move(select_child))
    {
        if(!(widening_constant > 0.0) || !std::isfinite(widening_constant))
            throw std::invalid_argument("invalid progressive widening constant");
        if(!(widening_alpha > 0.0 && widening_alpha <= 1.0)
           || !std::isfinite(widening_alpha))
            throw std::invalid_argument("invalid progressive widening alpha");
        if(seed) rng.emplace(*seed);
    }

    // A caller with a precomputed context-specific order may provide it here.
    static void set_ordering(node_t *action_root, Order order)
    {
        if(!action_root || !action_root->is_nodal())
            throw std::invalid_argument("ordering requires a nodal action root");
        action_root->get_info().tree_policy_data.ordering = std::move(order);
        action_root->get_info().tree_policy_data.ordering_injected = true;
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
            auto *child = child_selection(node);
            if(!child) return node;
            node = child;
        }
        return nullptr;
    }

    template<class Observer>
    node_t *complete_to_ceiling(node_t *node, std::stop_token stop, Observer &)
    {
        if(!node || stop.stop_requested()) return nullptr;
        while(node && !stop.stop_requested())
        {
            if(node->is_ceiling()) return node;
            if(node->get_children().empty())
            {
                auto index = with_order(node, stop, [&](const auto &order) {
                    return uct_detail::ordered_search(node, order, stop);
                });
                if(!index) return nullptr;
                node->get_info().tree_policy_data.all_children_included = false;
            }
            node = with_order(node, stop, [&](const auto &order) {
                return uct_detail::first_ordered_child(node, order, false);
            });
        }
        return nullptr;
    }
};

#endif
