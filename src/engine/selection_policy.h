#ifndef SELECTION_POLICY_H
#define SELECTION_POLICY_H

#include "finetree.h"

#include <cstddef>

struct most_visited_selection
{
    template<class Node, class Observer>
    Node *select_ceiling(Node *node, Observer &) const
    {
        while(node && !node->is_ceiling())
        {
            Node *best = nullptr;
            for(auto *child : node->get_children())
            {
                if(child->get_info().visits &&
                   (!best || child->get_info().visits > best->get_info().visits))
                    best = child;
            }
            if(!best) return finetree_nearby_ceiling(node);
            node = best;
        }
        return node;
    }
};

struct mean_reward_selection
{
    template<class Node, class Observer>
    Node *select_ceiling(Node *node, Observer &) const
    {
        if(!node) return nullptr;
        const double sign = node->get_player() ? -1.0 : 1.0;
        while(!node->is_ceiling())
        {
            Node *best = nullptr;
            double best_mean = 0;
            for(auto *child : node->get_children())
            {
                const auto &info = child->get_info();
                if(!info.visits) continue;
                const double mean = sign * info.sum_reward / info.visits;
                if(!best || mean > best_mean ||
                    (mean == best_mean && info.visits > best->get_info().visits))
                {
                    best = child;
                    best_mean = mean;
                }
            }
            if(!best) return finetree_nearby_ceiling(node);
            node = best;
        }
        return node;
    }
};

// A prefix visit count bounds the visits of any completed action below it.
struct completion_aware_v0_selection
{
    double minimum_prefix_fraction = 0.95;
    std::size_t minimum_completion_lead = 4;

private:
    template<class Node>
    static void find_peak(const Node *node, std::size_t &peak)
    {
        if(node->get_info().visits <= peak) return;
        if(node->is_ceiling())
            peak = node->get_info().visits;
        else
            for(auto *child : node->get_children()) find_peak(child, peak);
    }

public:
    template<class Node, class Observer>
    Node *select_ceiling(Node *node, Observer &) const
    {
        while(node && !node->is_ceiling())
        {
            auto children = node->get_children();
            Node *leader = nullptr;
            std::size_t leading_visits = 0;
            for(auto *child : children)
            {
                if(child->get_info().visits > leading_visits)
                {
                    leader = child;
                    leading_visits = child->get_info().visits;
                }
            }
            if(!leader) return finetree_nearby_ceiling(node);

            const double cutoff = minimum_prefix_fraction * leading_visits;
            Node *best = leader;
            std::size_t leader_peak = 0, best_peak = 0;
            bool compared = false;
            for(auto *child : children)
            {
                const auto visits = child->get_info().visits;
                if(child == leader || !visits || visits < cutoff) continue;
                if(!compared)
                {
                    find_peak(leader, leader_peak);
                    best_peak = leader_peak;
                    compared = true;
                }
                std::size_t peak = 0;
                find_peak(child, peak);
                if(peak > best_peak || (peak == best_peak && visits > best->get_info().visits))
                {
                    best = child;
                    best_peak = peak;
                }
            }
            node = best_peak - leader_peak >= minimum_completion_lead ? best : leader;
        }
        return node;
    }
};

#endif
