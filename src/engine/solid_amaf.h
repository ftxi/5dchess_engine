#ifndef SOLID_AMAF_H
#define SOLID_AMAF_H

#include <map>
#include <memory>
#include <utility>

#include "solid.h"

struct solid_amaf_node_data
{
    struct statistic
    {
        float sum_reward = 0.0f;
        std::size_t visits = 0;
    };
    using table = std::map<std::pair<index_t, index_t>, statistic>;
    // Only a nodal action root owns a table. Coordinates are local to its HC.
    std::unique_ptr<table> amaf;
};

struct solid_amaf_backpropagation
{
    template<class Node, class Result, class Observer>
    void backpropagate(Node *ceiling, const Result &reward, Observer &observer) const
    {
        sum_backpropagation{}.backpropagate(ceiling, reward, observer);
        // An ignited ceiling's incoming edge still belongs to the previous action.
        while(ceiling->get_parent())
        {
            auto *root = uct_detail::action_root(ceiling->get_parent());
            auto &table = root->get_info().tree_policy_data.amaf;
            for(auto *edge = ceiling; edge != root && edge->get_n() != 0; edge = edge->get_parent())
            {
                // Each axis occurs once in the path and axis zero has only one prefix.
                if(!table) table = std::make_unique<solid_amaf_node_data::table>();
                auto &shared = (*table)[{edge->get_n(), edge->get_i()}];
                shared.sum_reward += reward.score;
                ++shared.visits;
            }
            ceiling = root;
        }
    }
};

enum class solid_amaf_blend { pooled, half_pooled, half_outside };

struct solid_amaf_child_selection
{
    solid_amaf_blend blend = solid_amaf_blend::pooled;

    float value_adjustment(float local_sum, std::size_t local_visits,
        const solid_amaf_node_data::statistic &shared) const
    {
        if(!local_visits || shared.visits <= local_visits) return 0.0f;
        constexpr float horizon = 32.0f;
        const float n = static_cast<float>(local_visits);
        const float local_mean = local_sum / n;
        const float decay = horizon / (horizon + n);
        if(blend == solid_amaf_blend::half_outside)
        {
            constexpr float confidence_half_count = 8.0f;
            const float m = static_cast<float>(shared.visits - local_visits);
            const float outside_mean = (shared.sum_reward - local_sum) / m;
            const float confidence = m / (m + confidence_half_count);
            return 0.5f * decay * confidence * (outside_mean - local_mean);
        }
        const float strength = blend == solid_amaf_blend::half_pooled ? 0.5f : 1.0f;
        const float pooled_mean = shared.sum_reward / static_cast<float>(shared.visits);
        return strength * decay * (pooled_mean - local_mean);
    }

    template<class Node>
    Node *operator()(Node *node) const
    {
        if(node->is_nodal() || node->get_children().size() < 2)
            return solid_child_selection{}(node);
        const auto &table = uct_detail::action_root(node)->get_info().tree_policy_data.amaf;
        if(!table) return solid_child_selection{}(node);
        return solid_child_selection{}(node, [&](const Node *child) {
            const auto &local = child->get_info();
            const auto found = table->find({child->get_n(), child->get_i()});
            if(found == table->end()) return 0.0f;
            return value_adjustment(local.sum_reward, local.visits, found->second);
        });
    }
};

using solid_amaf_tree_policy = progressive_widening_tree_policy<
    solid_HC_ordering, solid_amaf_child_selection, solid_amaf_node_data>;
using solid_amaf_mcts_engine = basic_mcts_engine<solid_amaf_tree_policy, zero_default_policy,
    solid_amaf_backpropagation, most_visited_selection, mcts_observer>;

class solid_amaf_engine final : public solid_amaf_mcts_engine
{
public:
    solid_amaf_engine(std::unique_ptr<io_handler> io, std::optional<std::uint32_t> seed,
        double widening_constant, double widening_alpha, std::size_t prefix_budget,
        solid_amaf_blend blend = solid_amaf_blend::pooled)
        : solid_amaf_mcts_engine({}, solid_amaf_tree_policy{seed, widening_constant, widening_alpha,
            solid_scores, solid_amaf_child_selection{blend}, HC_build_options{.remove_check = true}, prefix_budget},
            {}, {}, {}, std::move(io)) {}
};

#endif /* SOLID_AMAF_H */
