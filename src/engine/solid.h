#ifndef SOLID_H
#define SOLID_H

#include <algorithm>
#include <cmath>
#include "mcts_engines.h"

class solid_HC_ordering : public scored_HC_ordering
{
    move_score_table weights;

public:
    solid_HC_ordering(const HC &hc, move_score_table scores, std::mt19937 &rng)
        : scored_HC_ordering(hc, scores, rng), weights(std::move(scores))
    {
        // A 1024-point gap gives a relative exploration weight of 1/16.
        constexpr float score_half_life = 256.0f;
        for(auto &axis : weights)
            for(float &value : axis)
                value = std::exp2(value / score_half_life);
    }

    float weight(index_t axis, index_t coordinate) const { return weights[axis][coordinate]; }
};

struct solid_child_selection
{
    template<class Node, class Adjustment = uct_detail::no_value_adjustment>
    Node *operator()(Node *node, Adjustment adjustment = {}) const
    {
        const auto children = node->get_children();
        if(children.size() < 2) return uct_detail::best_uct_child(node);
        // Incoming coordinates belong to the parent's HC even after a child ignites.
        const auto &order = uct_detail::action_root(node)->get_info().tree_policy_data.ordering;
        if(!order) return uct_detail::best_uct_child(node, [](const Node *) { return 1.0f; }, adjustment);
        const auto weight = [&](const Node *child) {
            return order->weight(child->get_n(), child->get_i());
        };
        const auto &info = node->get_hc_info();
        const auto penalized = [&](const Node *child) {
            return info.is_optional_move(child->get_n(), child->get_i());
        };
        float best_weight = 0.0f;
        std::size_t optional = 0, ordinary = 0;
        for(const auto *child : children)
        {
            if(!child->get_info().registered || !child->get_info().visits) continue;
            best_weight = std::max(best_weight, weight(child));
            if(penalized(child)) ++optional;
            else ++ordinary;
        }
        const float optional_scale = ordinary > 0 && optional > ordinary
            ? std::sqrt(static_cast<float>(ordinary) / static_cast<float>(optional))
            : 1.0f;
        return uct_detail::best_uct_child(node, [&](const Node *child) {
            const float relative = weight(child) / best_weight;
            return optional_scale < 1.0f && penalized(child) ? relative * optional_scale : relative;
        }, adjustment);
    }
};

using solid_tree_policy = progressive_widening_tree_policy<solid_HC_ordering, solid_child_selection>;
using solid_mcts_engine = basic_mcts_engine<solid_tree_policy, zero_default_policy,
    sum_backpropagation, most_visited_selection, mcts_observer>;

class solid_engine final : public solid_mcts_engine
{
public:
    solid_engine(std::unique_ptr<io_handler> io, std::optional<std::uint32_t> seed,
        double widening_constant, double widening_alpha, std::size_t prefix_budget)
        : solid_mcts_engine({}, solid_tree_policy{seed, widening_constant, widening_alpha,
            solid_scores, {}, HC_build_options{.remove_check = true}, prefix_budget},
            {}, {}, {}, std::move(io)) {}
};

#endif /* SOLID_H */
