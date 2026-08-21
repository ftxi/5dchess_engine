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
    std::vector<std::unique_ptr<gnode>> children;

    gnode(gnode<T> *parent, std::optional<state> s, const action &act, const T &info)
        : parent(parent), s(s), act(act), info(info), children() {}

    std::string render_pgn_turn(
        const std::function<std::string(T)> &show,
        pgn_options show_flags,
        turn_t turn,
        bool full_turn_display,
        const action &witness,
        bool has_continuation
    );
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

    static constexpr pgn_options default_pgn_options = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION | pgn_options::SHOW_MATE;
    
    std::string pgn_tree(
        std::function<std::string(T)> show = [](T){return "";},
        pgn_options show_flags = default_pgn_options,
        turn_t start_turn = {1,false},
        bool full_turn_display=true
    );

    std::string pgn_path(
        std::function<std::string(T)> show = [](T){return "";},
        pgn_options show_flags = default_pgn_options
    );
};

#include "gametree.inl"

#endif /* GAMETREE_H */
