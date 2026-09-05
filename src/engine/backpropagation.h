#ifndef BACKPROPAGATION_H
#define BACKPROPAGATION_H

struct sum_backpropagation
{
    template<class Node, class Result, class Observer>
    void backpropagate(Node *node, const Result &reward, Observer &) const
    {
        for(; node; node = node->get_parent())
        {
            node->get_info().sum_reward += reward.score;
            ++node->get_info().visits;
        }
    }
};

#endif
