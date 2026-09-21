#ifndef MCTS_H
#define MCTS_H

#include <cassert>
#include <chrono>
#include <concepts>
#include <stdexcept>
#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "finetree.h"
#include "uci.h"

/*
This files define the behavioral boundaries for components of Monte Carlo tree search (MCTS).

A MCTS-based engine can be constructed by providing the following components:
- Tree policy: a class that implements the TreePolicy concept. It is responsible for selecting a node in the fine tree to expand and completing it to a ceiling node.
- Default policy: a class that implements the DefaultPolicy concept. It is responsible for evaluating a state at a ceiling node and returning a reward.
- Backpropagation policy: a class that implements the BackPropagation concept. It is responsible for backpropagating the reward from a ceiling node to the root node.
- Selection policy: a class that implements the SelectionPolicy concept. It is responsible for selecting the bestmove.
 */
template <typename T, typename Variant>
inline constexpr bool is_alternative_v = false;

template <typename T, typename... Ts>
inline constexpr bool is_alternative_v<T, std::variant<Ts...>> =
    (std::is_same_v<T, Ts> || ...);

// The concept
template <typename T, typename Variant>
concept is_variant_of = is_alternative_v<T, Variant>;

template<typename Policy, typename T>
    requires is_variant_of<T, engine::option_value_t>
struct policy_option
{
    using value_type = T;
    std::string_view key;
    void (Policy::*setter)(T);
};

template<typename Policy, typename T>
policy_option(std::string_view, void (Policy::*)(T))
    -> policy_option<Policy, T>;

struct option_dispatch_result
{
    std::size_t matched = 0;
    std::size_t dispatched = 0;

    option_dispatch_result &operator+=(const option_dispatch_result &other)
    {
        matched += other.matched;
        dispatched += other.dispatched;
        return *this;
    }
};

template<class Policy>
option_dispatch_result dispatch_watched_option(
    Policy &policy,
    std::string_view key,
    const engine::option_value_t &value)
{
    option_dispatch_result result;
    if constexpr(requires { policy.watched_options; })
    {
        std::apply([&](const auto &... option) {
            if constexpr(sizeof...(option) != 0)
            {
                auto dispatch = [&](const auto &candidate) {
                    if(candidate.key != key) return;
                    ++result.matched;
                    using option_t = std::remove_cvref_t<decltype(candidate)>;
                    if(const auto *typed
                       = std::get_if<typename option_t::value_type>(&value))
                    {
                        (policy.*candidate.setter)(*typed);
                        ++result.dispatched;
                    }
                };
                (dispatch(option), ...);
            }
        }, policy.watched_options);
    }
    return result;
}

template<class... Policies>
option_dispatch_result dispatch_watched_options(
    std::string_view key,
    const engine::option_value_t &value,
    Policies &... policies)
{
    option_dispatch_result result;
    ((result += dispatch_watched_option(policies, key, value)), ...);
    return result;
}

template<typename Details = std::monostate>
struct reward_t
{
    float score;
    [[no_unique_address]] Details data;
};

template<class TreePolicyData>
struct mcts_node_info
{
    float sum_reward = 0.0f;
    std::size_t visits = 0;
    bool registered = false;
    [[no_unique_address]] TreePolicyData tree_policy_data;
};

template<class TP>
using tree_node_t = fine_node<mcts_node_info<typename TP::node_data>>;

namespace event
{
// template<class Node>
// struct tree_policy_select
// {
//     std::size_t iteration;
//     const Node *node;
// };

// template<class Node>
// struct tree_policy_complete_to_ceiling
// {
//     std::size_t iteration;
//     const Node *node;
// };

// template<class Node, class Result>
// struct default_policy_evaluate
// {
//     std::size_t iteration;
//     const Node *node;
//     const Result &reward;
// };

template<class Node, class Result>
struct iteration_completed
{
    std::size_t iteration;
    const Node *node;
    const Node *ceiling_node;
    const std::optional<Result> &reward;
};

struct search_aborted
{
    std::size_t iteration;
    std::string reason;
};

template<class Node>
struct bestmove_selected
{
    std::size_t iteration;
    std::chrono::steady_clock::duration duration;
    const Node *selected_ceiling;
};
} /* namespace event */

template<class Observer, class Event, class Node, class Result>
concept MCTSObserver = requires(Observer &observer, Event event)
{
    {
        observer.watch(event)
    } -> std::same_as<void>;
    {
        observer.report()
    } -> std::same_as<std::string>;
};

template<class Observer>
void watch_event(Observer &observer, const auto &event)
{
    if constexpr(requires { observer.watch(event); })
    {
        observer.watch(event);
    }
}

template<class TP, class Observer>
concept TreePolicy = requires
{
    typename TP::node_data;
    requires std::default_initializable<typename TP::node_data>;
}
&& requires(
    TP &policy,
    tree_node_t<TP> *node,
    std::stop_token stop_token,
    Observer &observer
)
{
    {
        policy.select(node, stop_token, observer)
    } -> std::same_as<tree_node_t<TP> *>;
    // A non-null result must be a ceiling node. It may remain unignited until
    // the tree policy later needs to search from it.
    {
        policy.complete_to_ceiling(node, stop_token, observer)
    } -> std::same_as<tree_node_t<TP> *>;
};

namespace detail
{

template<class T>
struct is_reward : std::false_type {};

template<class Details>
struct is_reward<reward_t<Details>> : std::true_type {};

template<class T>
inline constexpr bool is_reward_v = is_reward<T>::value;

} /* namespace detail */

template<class DP, class Observer>
concept DefaultPolicy = requires
{
    typename DP::result_type;
    requires detail::is_reward_v<typename DP::result_type>;
}
&& requires(
    DP &policy,
    state position,
    std::stop_token stop_token,
    Observer &observer
)
{
    // A disengaged result means that no reward may be committed, normally
    // because evaluation was interrupted.
    {
        policy.evaluate(std::move(position), stop_token, observer)
    } -> std::same_as<std::optional<typename DP::result_type>>;
};

template<class BP, class Node, class Result, class Observer>
concept BackPropagation = requires(
    BP &policy,
    Node *node,
    const Result &reward,
    Observer &observer
)
{
    {
        policy.backpropagate(node, reward, observer)
    } -> std::same_as<void>;
};

template<class SP, class Node, class Observer>
concept SelectionPolicy = requires(
    SP &policy,
    Node *root,
    Observer &observer
)
{
    // A non-null result must be a ceiling node. This semantic postcondition is
    // asserted by basic_mcts_engine before converting its path to an action.
    {
        policy.select_ceiling(root, observer)
    } -> std::same_as<Node *>;
};

template<class TP, class DP, class BP, class SP, class Observer>
    requires TreePolicy<TP, Observer>
        && DefaultPolicy<DP, Observer>
        && BackPropagation<BP, tree_node_t<TP>, typename DP::result_type, Observer>
        && SelectionPolicy<SP, tree_node_t<TP>, Observer>
class basic_mcts_engine: public engine
{
public:
    using node_info = mcts_node_info<typename TP::node_data>;
    using node_t = fine_node<node_info>;

private:
    [[no_unique_address]] DP default_policy;
    [[no_unique_address]] TP tree_policy;
    [[no_unique_address]] BP backpropagation_policy;
    [[no_unique_address]] SP selection_policy;
    [[no_unique_address]] Observer observer;
    std::unique_ptr<node_t> root;
protected:
    void on_option_changed(const std::string &key, const option_value_t &value) override
    {
        const auto result = dispatch_watched_options(
            key, value, tree_policy, default_policy, backpropagation_policy,
            selection_policy, observer);
        if(result.matched != 0)
        {
            if(result.dispatched != result.matched)
            {
                send_debug_info("option type mismatch: " + key);
            }
            return;
        }
        engine::on_option_changed(key, value);
    }
public:
    basic_mcts_engine(
        DP default_policy,
        TP tree_policy,
        BP backpropagation_policy,
        SP selection_policy,
        Observer observer,
        std::unique_ptr<io_handler> io_handler
    ) : engine(std::move(io_handler)),
        default_policy(std::move(default_policy)),
        tree_policy(std::move(tree_policy)),
        backpropagation_policy(std::move(backpropagation_policy)),
        selection_policy(std::move(selection_policy)),
        observer(std::move(observer)),
        root(nullptr)
    {}

    std::optional<action> find_best_move(
        std::optional<int> depth_limit,
        std::optional<int> time_limit_ms,
        std::stop_token stop_token
    ) override;

    void initialize() override;
};

#include "mcts.inl"

#endif /* MCTS_H */
