#ifndef UCT_H
#define UCT_H

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <stop_token>
#include <utility>
#include <vector>

#include "mcts.h"

constexpr float exploration_constant = 1.4142135623730951f;

// Adversarial UCT score using rewards from White's fixed perspective.
float uct(float sum_reward, std::size_t visits,
          std::size_t parent_visits, bool maximizing_player);

namespace uct_detail
{
inline std::uint64_t next_policy_id()
{
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

inline std::mt19937 &random_engine(std::optional<std::mt19937> &seeded)
{
    if(seeded) return *seeded;
    static thread_local std::mt19937 fallback(std::random_device{}());
    return fallback;
}

template<class Node>
Node *action_root(Node *node)
{
    while(node->get_parent() && !node->is_nodal()) node = node->get_parent();
    return node;
}

template<class Node>
Node *best_uct_child(Node *node)
{
    Node *best = nullptr;
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

template<class Node, HCOrdering Order>
Node *first_ordered_child(Node *node, const Order &order,
                          bool unregistered_only)
{
    const auto children = node->get_children();
    if(children.empty()) return nullptr;
    integer_set candidates;
    for(auto *child : children)
        if(!unregistered_only || !child->get_info().registered)
            candidates.insert(child->get_i());
    Node *first = nullptr;
    const index_t axis = children.front()->get_n();
    order.for_each(axis, candidates, [&](index_t coordinate) {
        if(!first) first = node->get_child(coordinate);
    });
    return first;
}

template<class Node, HCOrdering Order>
std::optional<index_t> ordered_search(
    Node *node, const Order &order, std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    return node->search(order).first();
}
} // namespace uct_detail

struct uct_child_selection
{
    template<class Node>
    Node *operator()(Node *node) const
    {
        return uct_detail::best_uct_child(node);
    }
};

template<class Order>
concept ContextConstructibleHCOrdering = HCOrdering<Order> &&
    (std::constructible_from<Order,
         const HC_info &, std::mt19937 &, std::stop_token>
     || std::constructible_from<Order, const HC &,
         const std::vector<std::vector<float>> &, std::mt19937 &>
     || std::constructible_from<Order, const HC &, std::mt19937 &>
     || std::default_initializable<Order>);

using coordinate_score_function = std::optional<std::vector<std::vector<float>>>
    (*)(const HC_info &, std::stop_token);

// Keep HCOrdering's traversal concept independent of its construction. The
// score function supplies data to scored orderings; random and natural
// orderings retain their current constructors.
template<HCOrdering Order>
std::optional<Order> construct_hc_ordering(
    const HC_info &info, std::mt19937 &rng, std::stop_token stop = {},
    coordinate_score_function score = nullptr)
{
    if(stop.stop_requested()) return std::nullopt;
    if constexpr(std::constructible_from<Order,
                  const HC_info &, std::mt19937 &, std::stop_token>)
    {
        Order order(info, rng, stop);
        if(stop.stop_requested()) return std::nullopt;
        return order;
    }
    else if constexpr(std::constructible_from<Order, const HC &,
                      const std::vector<std::vector<float>> &, std::mt19937 &>)
    {
        if(!score) return std::nullopt;
        auto values = score(info, stop);
        if(!values || stop.stop_requested()) return std::nullopt;
        return Order(info.universe, *values, rng);
    }
    else if constexpr(std::constructible_from<Order, const HC &, std::mt19937 &>)
        return Order(info.universe, rng);
    else if constexpr(std::default_initializable<Order>)
        return Order{};
    else
        return std::nullopt; // May instead be supplied to progressive widening.
}

template<HCOrdering Order>
struct uct_node_data
{
    bool all_children_included = false;
    bool fully_expanded = false;
    // Scored orderings are cached; random order is rebuilt to keep
    // the completion behavior of the original UCT policy.
    std::optional<Order> ordering;
    std::uint64_t ordering_owner = 0;

    uct_node_data() = default;
    uct_node_data(const uct_node_data &) = delete;
    uct_node_data &operator=(const uct_node_data &) = delete;
    uct_node_data(uct_node_data &&) noexcept = default;
    uct_node_data &operator=(uct_node_data &&) noexcept = default;
};

template<HCOrdering Order = random_HC_ordering>
    requires ContextConstructibleHCOrdering<Order>
class uct_tree_policy
{
public:
    using node_data = uct_node_data<Order>;
    using node_t = tree_node_t<uct_tree_policy<Order>>;

private:
    std::optional<std::mt19937> rng;
    std::uint64_t policy_id;
    coordinate_score_function score_function;

    template<class F>
    auto with_order(node_t *node, std::stop_token stop, F &&use)
        -> decltype(use(std::declval<const Order &>()))
    {
        using result_t = decltype(use(std::declval<const Order &>()));
        if(stop.stop_requested()) return result_t{};
        if constexpr(std::constructible_from<Order,
                     const HC_info &, std::mt19937 &, std::stop_token>
                     || std::constructible_from<Order, const HC &,
                         const std::vector<std::vector<float>> &, std::mt19937 &>)
        {
            auto &cached = uct_detail::action_root(node)
                ->get_info().tree_policy_data.ordering;
            auto &owner = uct_detail::action_root(node)
                ->get_info().tree_policy_data.ordering_owner;
            if(!cached || owner != policy_id)
            {
                auto built = construct_hc_ordering<Order>(
                    node->get_context()->hc_info,
                    uct_detail::random_engine(rng), stop, score_function);
                if(!built) return result_t{};
                cached = std::move(*built);
                owner = policy_id;
            }
            if(stop.stop_requested()) return result_t{};
            return use(*cached);
        }
        else
        {
            auto built = construct_hc_ordering<Order>(
                node->get_context()->hc_info,
                uct_detail::random_engine(rng), stop, score_function);
            if(!built) return result_t{};
            return use(*built);
        }
    }

    node_t *expand(node_t *node, std::stop_token stop)
    {
        auto &data = node->get_info().tree_policy_data;
        if(data.fully_expanded || stop.stop_requested()) return nullptr;
        if(!data.all_children_included)
        {
            auto *child = with_order(node, stop, [&](const Order &order) {
                return uct_detail::first_ordered_child(node, order, true);
            });
            if(child)
            {
                child->get_info().registered = true;
                return child;
            }
            if(stop.stop_requested()) return nullptr;
            data.all_children_included = true;
        }
        if(node->is_ceiling() && !node->is_nodal()) node->ignite();
        if(stop.stop_requested()) return nullptr;
        auto index = with_order(node, stop, [&](const Order &order) {
            return uct_detail::ordered_search(node, order, stop);
        });
        if(index)
        {
            auto *child = node->get_child(*index);
            child->get_info().registered = true;
            data.all_children_included = false;
            return child;
        }
        if(!stop.stop_requested()) data.fully_expanded = true;
        return nullptr;
    }

public:
    explicit uct_tree_policy(
        std::optional<std::uint32_t> seed = std::nullopt,
        coordinate_score_function score = nullptr)
        : policy_id(uct_detail::next_policy_id()), score_function(score)
    {
        if constexpr(std::constructible_from<Order, const HC &,
                     const std::vector<std::vector<float>> &, std::mt19937 &>
                     && !std::constructible_from<Order,
                         const HC_info &, std::mt19937 &, std::stop_token>)
            if(!score_function)
                throw std::invalid_argument("scored ordering requires a score function");
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
            auto *child = uct_detail::best_uct_child(node);
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
            if(node->get_children().empty())
            {
                auto index = with_order(node, stop, [&](const Order &order) {
                    return uct_detail::ordered_search(node, order, stop);
                });
                if(!index) return nullptr;
                node->get_info().tree_policy_data.all_children_included = false;
            }
            node = with_order(node, stop, [&](const Order &order) {
                return uct_detail::first_ordered_child(node, order, false);
            });
        }
        return nullptr;
    }
};

#endif // UCT_H
