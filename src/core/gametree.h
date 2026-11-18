#ifndef GAMETREE_H
#define GAMETREE_H

#include <algorithm>
#include <functional>
#include <set>
#include <optional>
#include <sstream>
#include <stdexcept>
#include "action.h"
#include "state.h"
#include "turn.h"

template<typename T = std::monostate>
class gnode {
    gnode<T> *parent;
    std::optional<state> s;
    action act;
    T info;
    bool illegal_history;
    // for ownership and display order
    std::vector<std::unique_ptr<gnode>> children;
    struct child_action_less {
        using is_transparent = void;
        bool operator()(const gnode *a, const gnode *b) const { return a->act < b->act; }
        bool operator()(const gnode *a, const action &b) const { return a->act < b; }
        bool operator()(const action &a, const gnode *b) const { return a < b->act; }
    };
    // for lookup
    std::set<gnode *, child_action_less> child_index;

    gnode(gnode<T> *parent, std::optional<state> s, action act, T info,
          bool illegal_history = false)
        : parent(parent), s(std::move(s)), act(std::move(act)), info(std::move(info)),
          illegal_history(illegal_history || (parent && parent->has_illegal_history())), children() {}

    std::string render_pgn_turn(
        const std::function<std::string(const T &)> &show,
        pgn_options show_flags,
        turn turn,
        bool full_turn_display,
        const action &witness,
        bool has_continuation
    );
public:
    ~gnode() noexcept
    {
        // Walk down existing ownership links and back up parent links. Delete
        // only empty nodes, retaining descendant-before-parent destruction.
        auto *node = this;
        for(;;)
        {
            node->child_index.clear();
            if(!node->children.empty())
            {
                node = node->children.back().get();
                continue;
            }
            if(node == this) break;
            auto *parent = node->parent;
            parent->children.pop_back();
            node = parent;
        }
    }

    // Callers classify royal safety once at insertion; trusted legal search can
    // use the default without rescanning. Descendants always inherit the flag.
    static std::unique_ptr<gnode<T>> create_root(state s, T info, bool illegal_history = false)
    {
        return std::unique_ptr<gnode<T>>(new gnode<T>(nullptr, std::move(s), action{}, std::move(info), illegal_history));
    }
    
    static std::unique_ptr<gnode<T>> create_child(gnode<T> *parent, std::optional<state> s, action act, T info,
                                               bool illegal_history = false)
    {
        return std::unique_ptr<gnode<T>>(new gnode<T>(parent, std::move(s), std::move(act), std::move(info), illegal_history));
    }

    std::unique_ptr<gnode<T>> clone(gnode<T> *new_parent = nullptr) const
    {
        auto node = std::unique_ptr<gnode<T>>(
            new gnode<T>(new_parent, s, act, info, illegal_history)
        );
        node->children.reserve(children.size());
        for (const auto &child : children) {
            node->add_child(child->clone(node.get()));
        }
        return node;
    }

    // Return a state copy, caching reconstruction; report invalid stored actions.
    state get_state();
    const state &get_stored_state() const { return s.value(); }
    const action &get_action() const { return act; }
    bool has_illegal_history() const noexcept { return illegal_history; }
    T &get_info() { return info; }
    const T &get_info() const { return info; }
    void set_info(const T &x) { info = x; }
    void set_info(T &&x) { info = std::move(x); }
    gnode<T> *get_parent() const { return parent; }

    // Return nullptr for a duplicate action, leaving the existing child intact.
    gnode<T> *add_child(std::unique_ptr<gnode<T>> child) 
    {
        if(!child)
        {
            throw std::invalid_argument("gnode::add_child(): Child is null");
        }
        if(child->parent != this)
        {
            throw std::invalid_argument("gnode::add_child(): Child has a different parent");
        }
        const auto [entry, inserted] = child_index.insert(child.get());
        if(!inserted) return nullptr;
        try
        {
            children.push_back(std::move(child));
        }
        catch(...)
        {
            child_index.erase(entry);
            throw;
        }
        return children.back().get();
    }

    const std::vector<std::unique_ptr<gnode>> &get_children() const 
    {
        return children;
    }

    // Erasing the owning pointer also destroys the complete descendant branch.
    bool delete_child(const action &a)
    {
        const auto entry = child_index.find(a);
        if(entry == child_index.end()) return false;
        auto *target = *entry;
        const auto owned = std::find_if(children.begin(), children.end(),
            [target](const auto &child) { return child.get() == target; });
        child_index.erase(entry);
        children.erase(owned);
        return true;
    }

    gnode<T> *find_child(const action &a)
    {
        const auto entry = child_index.find(a);
        return entry == child_index.end() ? nullptr : *entry;
    }

    const gnode<T> *find_child(const action &a) const
    {
        const auto entry = child_index.find(a);
        return entry == child_index.end() ? nullptr : *entry;
    }

    static constexpr pgn_options default_pgn_options = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION | pgn_options::SHOW_MATE;
    
    std::string pgn_tree(
        const std::function<std::string(const T &)> &show = [](const T &){return "";},
        pgn_options show_flags = default_pgn_options,
        turn start_turn = {1,false},
        bool full_turn_display=true
    );

    std::string pgn_path(
        const std::function<std::string(const T &)> &show = [](const T &){return "";},
        pgn_options show_flags = default_pgn_options
    );
};

#include "gametree.inl"

#endif /* GAMETREE_H */
