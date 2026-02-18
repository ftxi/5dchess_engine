#ifndef GAMETREE_H
#define GAMETREE_H

#include <functional>
#include <optional>
#include <sstream>
#include "action.h"
#include "state.h"
#include "turn.h"

template<typename T>
class gnode {
    gnode<T> *parent;
    std::optional<state> s;
    action act;
    T info;
    std::vector<std::unique_ptr<gnode>> children;

    gnode(gnode<T>* parent, std::optional<state> s, const action &act, const T &info) : parent(parent), s(s), act(act), info(info), children() {}
public:
    static std::unique_ptr<gnode<T>> create_root(const state &s, const T &info)
    {
        return std::unique_ptr<gnode<T>>(new gnode<T>(nullptr, s, action{}, info));
    }
    
    static std::unique_ptr<gnode<T>> create_child(gnode<T>* parent, std::optional<state> s, const action &act, const T &info)
    {
        return std::unique_ptr<gnode<T>>(new gnode<T>(parent, s, act, info));
    }
    std::unique_ptr<gnode<T>> clone(gnode<T>* new_parent = nullptr) const
    {
        auto node = std::unique_ptr<gnode<T>>(
            new gnode<T>(new_parent, s, act, info)
        );
        for (const auto& child : children) {
            node->children.push_back(child->clone(node.get()));
        }
        return node;
    }
    state get_state()
    {
        if(s)
        {
            return *s;
        }
        else if(parent)
        {
            s = *parent->get_state().can_apply(act);
            return *s;
        }
        else
        {
            throw std::runtime_error("gnode::get_state(): Root gnode has no state");
        }
    }
    const action& get_action() const {
        return act;
    }
    const T& get_info() const {
        return info;
    }
    void set_info(const T &x)
    {
        info = x;
    }
    void set_info(T &&x)
    {
        info = x;
    }
    gnode<T>* get_parent() const {
        return parent;
    }
    gnode<T>* add_child(std::unique_ptr<gnode<T>> child) {
        children.push_back(std::move(child));
        return children.back().get();
    }
    const std::vector<std::unique_ptr<gnode>>& get_children() const {
        return children;
    }
    gnode<T>* find_child(const action &a)
    {
        for(const auto &child : children)
        {
            if(a == child->act)
            {
                return child.get();
            }
        }
        return nullptr;
    }
    std::string to_string(
        std::function<std::string(T)> show = [](T){return "";},
        uint16_t show_flags = state::SHOW_CAPTURE | state::SHOW_PROMOTION | state::SHOW_MATE,
        turn_t start_turn = {1,false},
        bool full_turn_display=true
    )
    {
        std::ostringstream oss;
        size_t num_children = children.size();
        if(parent) // non-root
        {
            auto [t, c] = start_turn;
            if(full_turn_display)
            {
                oss << t << (c?'b':'w')  << ". ";
            }
            else if(c)
            {
                oss << "/ ";
            }
            else
            {
                oss << t << ". ";
            }
            oss << parent->get_state().pretty_action(act, show_flags) << " ";
            oss << show(info);
            start_turn = next_turn(start_turn);
            if(c && num_children > 0)
            {
                oss << '\n';
            }
        }
        else
        {
            oss << show(info) << "\n";
        }
        if(num_children > 1)
        {
            for(auto it = children.begin(); it+1 != children.end(); it++)
            {
                oss << "(" << (**it).to_string(show, show_flags, start_turn, true) << ")\n";
            }
        }
        if(num_children > 0)
        {
            auto it = (children.end() - 1);
            oss << (**it).to_string(show, show_flags, start_turn, num_children > 1);
        }
        return oss.str();
    }
};

#endif /* GAMETREE_H */
