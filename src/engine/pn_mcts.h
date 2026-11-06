#ifndef PN_MCTS_H
#define PN_MCTS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "backpropagation.h"
#include "progressive_widening.h"
#include "rollout_policy.h"

inline constexpr double default_pn_mcts_constant = 1.0;

// Numbers prove a win for the search root's player. A draw disproves that
// goal, but is still backed up as zero reward, rather than as a loss.
struct pn_node_data
{
    using number = std::uint64_t;
    static constexpr number infinity = std::numeric_limits<number>::max();
    number proof = 1;
    number disproof = 1;
    bool terminal = false;
    bool goal_player = false;
};

namespace pn_detail
{
inline pn_node_data::number add(
    pn_node_data::number a, pn_node_data::number b)
{
    return b >= pn_node_data::infinity - a ? pn_node_data::infinity : a + b;
}

template<class Node>
void update(Node *node, bool goal_player)
{
    auto &data = node->get_info().tree_policy_data;
    data.goal_player = goal_player;
    if(data.terminal) return;

    // Fine-tree edges can be semimoves of the SAME player's action. Query
    // the actual decision maker instead of alternating AND/OR by tree depth.
    const bool is_or = node->get_player() == goal_player;
    auto minimum = pn_node_data::infinity;
    pn_node_data::number sum = 0;
    for(auto *child : node->get_children())
    {
        const auto &numbers = child->get_info().tree_policy_data;
        minimum = std::min(minimum, is_or ? numbers.proof : numbers.disproof);
        sum = add(sum, is_or ? numbers.disproof : numbers.proof);
    }
    // Widening may have deferred arbitrarily many successors. One virtual
    // unknown child is a conservative lower bound until exhaustive expansion
    // succeeds. This also covers an unignited or unevaluated leaf. In
    // particular, explored losses cannot disprove a partially widened OR,
    // and explored wins cannot prove a partially widened AND.
    if(!data.fully_expanded || node->get_children().empty())
    {
        minimum = std::min(minimum, pn_node_data::number{1});
        sum = add(sum, 1);
    }
    data.proof = is_or ? minimum : sum;
    data.disproof = is_or ? sum : minimum;
}
} /* namespace pn_detail */

class pn_child_selection
{
    double constant;

public:
    explicit pn_child_selection(double constant_ = default_pn_mcts_constant)
        : constant(constant_)
    {
        if(!(constant >= 0.0) || !std::isfinite(constant))
            throw std::invalid_argument("invalid PN-MCTS constant");
    }

    template<class Node>
    Node *operator()(Node *node) const
    {
        if(constant == 0.0) return uct_detail::best_uct_child(node);
        const auto &data = node->get_info().tree_policy_data;
        const bool is_or = node->get_player() == data.goal_player;
        const auto number = [is_or](const Node *child) {
            const auto &numbers = child->get_info().tree_policy_data;
            return is_or ? numbers.proof : numbers.disproof;
        };
        std::vector<pn_node_data::number> ranks;
        for(auto *child : node->get_children())
            if(child->get_info().registered && child->get_info().visits)
                ranks.push_back(number(child));
        if(ranks.empty()) return nullptr;
        std::sort(ranks.begin(), ranks.end());
        ranks.erase(std::unique(ranks.begin(), ranks.end()), ranks.end());

        Node *best = nullptr;
        double best_score = -std::numeric_limits<double>::infinity();
        const bool maximizing = !node->get_player();
        for(auto *child : node->get_children())
        {
            const auto &info = child->get_info();
            if(!info.registered || !info.visits) continue;
            const auto rank = 1 + (std::lower_bound(
                ranks.begin(), ranks.end(), number(child)) - ranks.begin());
            const float base = uct(info.sum_reward, info.visits,
                node->get_info().visits, maximizing);
            const double score = (maximizing ? base : -base)
                + constant * (1.0 - static_cast<double>(rank) / ranks.size());
            if(!best || score > best_score)
            {
                best = child;
                best_score = score;
            }
        }
        return best;
    }
};

template<HCOrdering Order = random_HC_ordering>
using pn_mcts_tree_policy = progressive_widening_tree_policy<
    Order, pn_child_selection, pn_node_data>;

struct pn_backpropagation
{
    template<class Node, class Observer>
    void backpropagate(
        Node *node, const reward_t<rollout_details> &reward, Observer &observer) const
    {
        sum_backpropagation{}.backpropagate(node, reward, observer);
        auto *root = node;
        while(root->get_parent()) root = root->get_parent();
        const bool goal_player = root->get_player();
        // Only an actual terminal tree leaf establishes a proof. A rollout
        // result or a zero cutoff evaluation must never solve the tree node.
        using end = rollout_details::termination;
        if(reward.data.num_actions == 0 && reward.data.end != end::ACTION_LIMIT)
        {
            auto &data = node->get_info().tree_policy_data;
            data.terminal = true;
            const bool won = reward.data.end
                == (goal_player ? end::BLACK_WINS : end::WHITE_WINS);
            data.proof = won ? 0 : pn_node_data::infinity;
            data.disproof = won ? pn_node_data::infinity : 0;
        }
        for(; node; node = node->get_parent()) pn_detail::update(node, goal_player);
    }
};

#endif /* PN_MCTS_H */
