#ifndef SELECTION_POLICY_H
#define SELECTION_POLICY_H

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
            if(!best) return node->get_nearby_ceiling();
            node = best;
        }
        return node;
    }
};

#endif
