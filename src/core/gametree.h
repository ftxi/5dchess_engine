#ifndef GAMETREE_H
#define GAMETREE_H

#include <functional>
#include <optional>
#include <sstream>
#include "action.h"
#include "state.h"
#include "turn.h"

template<typename T = std::monostate>
class gnode {
    gnode<T> *parent;
    std::optional<state> s;
    action act;
    T info;
    std::optional<pgnparser_ast::token_t> outcome;
    std::vector<std::unique_ptr<gnode>> children;

    gnode(gnode<T> *parent, std::optional<state> s, const action &act, const T &info)
        : parent(parent), s(s), act(act), info(info), outcome(std::nullopt), children() {}
public:
    static std::unique_ptr<gnode<T>> create_root(const state &s, const T &info)
    {
        return std::unique_ptr<gnode<T>>(new gnode<T>(nullptr, s, action{}, info));
    }
    
    static std::unique_ptr<gnode<T>> create_child(gnode<T> *parent, std::optional<state> s, const action &act, const T &info)
    {
        return std::unique_ptr<gnode<T>>(new gnode<T>(parent, s, act, info));
    }

    std::unique_ptr<gnode<T>> clone(gnode<T> *new_parent = nullptr) const
    {
        auto node = std::unique_ptr<gnode<T>>(
            new gnode<T>(new_parent, s, act, info)
        );
        node->outcome = outcome;
        for (const auto& child : children) {
            node->children.push_back(child->clone(node.get()));
        }
        return node;
    }

    state get_state();
    const action &get_action() const { return act; }
    T &get_info() { return info; }
    const T &get_info() const { return info; }
    void set_info(const T &x) { info = x; }
    void set_info(T &&x) { info = x; }
    std::optional<pgnparser_ast::token_t> get_outcome() const { return outcome; }
    void set_outcome(pgnparser_ast::token_t x) { outcome = x; }
    gnode<T> *get_parent() const { return parent; }

    gnode<T> *add_child(std::unique_ptr<gnode<T>> child) 
    {
        children.push_back(std::move(child));
        return children.back().get();
    }

    const std::vector<std::unique_ptr<gnode>> &get_children() const 
    {
        return children;
    }

    gnode<T> *find_child(const action &a)
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
        pgn_options show_flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION | pgn_options::SHOW_MATE,
        turn_t start_turn = {1,false},
        bool full_turn_display=true
    );
};

#include "gametree.inl"

#endif /* GAMETREE_H */
