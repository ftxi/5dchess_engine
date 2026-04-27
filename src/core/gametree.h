#ifndef GAMETREE_H
#define GAMETREE_H

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "action.h"
#include "state.h"
#include "turn.h"
#include "hypercuboid.h"

template<typename T>
class gnode;

std::unique_ptr<gnode<std::vector<std::string>>> build_gametree_from_pgn(std::string input);

template<typename T>
class gnode {
    gnode<T> *parent;
    mutable std::optional<state> s;
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

    state get_state() const
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
    const action& get_action() const { return act; }
    const T& get_info() const { return info; }
    void set_info(const T &x) { info = x; }
    void set_info(T &&x) { info = x; }
    gnode<T>* get_parent() const { return parent; }

    gnode<T>* add_child(std::unique_ptr<gnode<T>> child) 
    {
        children.push_back(std::move(child));
        return children.back().get();
    }

    std::optional<action> new_child()
    {
        const state &s = get_state();
        auto [w, ss] = HC_info::build_HC(s);
        for(moveseq mvs : w.search(ss))
        {
            std::vector<ext_move> emvs;
            std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
                return ext_move(m);
            });
            action act = action::from_vector(emvs, s);
            if(!find_child(act))
            {
                add_child(create_child(this, s.can_apply(act), act, T{}));
                return act;
            }
        }
        return std::nullopt;
    }

    std::vector<gnode<T>*> get_children() const 
    {
        std::vector<gnode<T>*> result;
        result.reserve(children.size());
        for (const auto& child : children)
        {
            result.push_back(child.get());
        }
        return result;
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
    ) const
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
        /* indent by 1 space * number of nested parentheses
        do not indent inside comments */
        int parentheses_level = 0;
        int curly_brace_level = 0;
        std::string result;
        for(char c : oss.str())
        {
            result += c;
            if(c == '(')
            {
                parentheses_level++;
            }
            else if(c == ')')
            {
                parentheses_level--;
            }
            else if(c == '{')
            {
                curly_brace_level++;
            }
            else if(c == '}')
            {
                curly_brace_level--;
            }
            else
            {
                if(curly_brace_level == 0 && c == '\n')
                {
                    result += std::string(parentheses_level, ' ');
                }
            }
        }
        return result;
    }

    match_status_t get_match_status() const
    {
        const state &s = get_state();
        return s.get_match_status();
    }
};

#endif /* GAMETREE_H */