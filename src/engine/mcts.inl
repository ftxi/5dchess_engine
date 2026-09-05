
namespace detail
{

template<class Node>
state state_at_ceiling(Node &node)
{
    assert(node.is_ceiling());
    if(node.is_nodal())
    {
        return node.get_context()->hc_info.s;
    }

    state result = node.get_context()->hc_info.s;
    for(full_move move : node.to_action())
    {
        [[maybe_unused]] const bool applied = result.apply_move<true>(move);
        assert(applied);
    }
    result.submit<true>();
    return result;
}

} /* namespace detail */

template <class TP, class DP, class BP, class SP, class Observer>
    requires TreePolicy<TP, Observer>
        && DefaultPolicy<DP, Observer>
        && BackPropagation<
            BP,
            tree_node_t<TP>,
            typename DP::result_type,
            Observer>
        && SelectionPolicy<SP, tree_node_t<TP>, Observer>
inline void basic_mcts_engine<TP, DP, BP, SP, Observer>::initialize()
{
    root = nullptr;
}

template <class TP, class DP, class BP, class SP, class Observer>
    requires TreePolicy<TP, Observer>
        && DefaultPolicy<DP, Observer>
        && BackPropagation<
            BP,
            tree_node_t<TP>,
            typename DP::result_type,
            Observer>
        && SelectionPolicy<SP, tree_node_t<TP>, Observer>
inline std::optional<action>
basic_mcts_engine<TP, DP, BP, SP, Observer>::find_best_move(
    std::optional<int> depth_limit,
    std::optional<int> time_limit_ms,
    std::stop_token stop_token
)
{
    constexpr static int DEPTH_TO_ITERATION_MULTIPLIER = 10; // if depth limit is set, iteration_limit = depth_limit * DEPTH_TO_ITERATION_MULTIPLIER

    const auto time_search_start = std::chrono::steady_clock::now();
    const auto time_stop = time_limit_ms.has_value()
        ? time_search_start + std::chrono::milliseconds(time_limit_ms.value())
        : std::chrono::steady_clock::time_point::max();
    // Convert depth_limit to iteration budget if provided
    std::optional<std::size_t> iteration_limit;
    if(depth_limit.has_value())
    {
        iteration_limit = static_cast<std::size_t>(depth_limit.value()) * DEPTH_TO_ITERATION_MULTIPLIER;
    }

    if constexpr(requires { observer.reset(); }) observer.reset();
    root = node_t::make_root(*get_current_state());
    std::size_t iteration_count = 0;
    while(true)
    {
        if(stop_token.stop_requested())
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "stop requested"
            });
            break;
        }
        // Check iteration limit and time limit
        if(iteration_limit.has_value() && iteration_count >= iteration_limit.value())
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "iteration limit reached"
            });
            break;
        }
        if(time_limit_ms.has_value() && std::chrono::steady_clock::now() >= time_stop)
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "time limit reached"
            });
            break;
        }
        // core logic
        node_t *node = tree_policy.select(root.get(), stop_token, observer);
        if(stop_token.stop_requested())
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "stop requested during tree policy selection"
            });
            break;
        }
        if(node == nullptr)
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "tree policy returned no node"
            });
            break;
        }
        node_t *ceiling_node = tree_policy.complete_to_ceiling(
            node,
            stop_token,
            observer);
        if(stop_token.stop_requested())
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "stop requested during ceiling completion"
            });
            break;
        }
        if(ceiling_node == nullptr)
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = "tree policy could not complete to a ceiling"
            });
            break;
        }
        assert(ceiling_node->is_ceiling());
        std::optional<typename DP::result_type> result = default_policy.evaluate(
            detail::state_at_ceiling(*ceiling_node),
            stop_token,
            observer);
        if(!result.has_value())
        {
            watch_event(observer, event::search_aborted{
                .iteration = iteration_count,
                .reason = stop_token.stop_requested()
                    ? "stop requested during default policy evaluation"
                    : "default policy returned no result"
            });
            break;
        }
        // A completed evaluation is committed as one indivisible iteration.
        backpropagation_policy.backpropagate(
            ceiling_node,
            *result,
            observer);
        watch_event(observer, event::iteration_completed{
            .iteration = iteration_count,
            .node = node,
            .ceiling_node = ceiling_node,
            .reward = result
        });
        iteration_count++;
    }
    
    // Final selection must still run after a search stop request.
    node_t *best_node = selection_policy.select_ceiling(root.get(), observer);
    watch_event(observer, event::bestmove_selected{
        .iteration = iteration_count,
        .duration = std::chrono::steady_clock::now() - time_search_start,
        .selected_ceiling = best_node
    });
    if constexpr(requires { observer.report(); })
    {
        std::istringstream report(observer.report());
        for(std::string line; std::getline(report, line);)
            if(!line.empty()) send_info(line);
    }
    if(best_node == nullptr)
    {
        return std::nullopt; // No best node found
    }
    assert(best_node->is_ceiling());
    const state &s = root->get_context()->hc_info.s;
    action best_action = action::from_moveseq(best_node->to_action(), s);
    return best_action;
}
