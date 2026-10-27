#ifndef HC_TREE_H
#define HC_TREE_H

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>
#include "geometry.h"

// Experimental positive-space index. Internal HCs bound complete disjoint
// partitions; negative constraints belong to the caller, not to these cells.
class hc_tree
{
    struct cell
    {
        HC bounds;
        std::vector<std::unique_ptr<cell>> children;
        explicit cell(HC hc) : bounds(std::move(hc)) {}
    };
    using link = std::unique_ptr<cell>;
    std::vector<link> roots;
    std::vector<link *> pending_path;
    std::size_t leaves = 0;

    static void compact(link &entry)
    {
        if(!entry) return;
        auto &children = entry->children;
        std::erase_if(children, [](const link &child) { return !child; });
        if(children.size() == 1)
        {
            auto child = std::move(children.front());
            entry = std::move(child);
        }
    }

    void finish_pending()
    {
        if(pending_path.empty()) return;
        pending_path.back()->reset();
        for(std::size_t i = pending_path.size() - 1; i > 0; --i)
        {
            auto &parent = *pending_path[i - 1];
            std::erase_if(parent->children, [](const link &child) { return !child; });
            if(parent->children.empty()) parent.reset();
            else compact(parent);
        }
        pending_path.clear();
        std::erase_if(roots, [](const link &root) { return !root; });
    }

    void replace(link &entry, search_space pieces)
    {
        pieces.prune_empty();
        leaves += pieces.size();
        if(pieces.empty()) entry.reset();
        else if(pieces.size() == 1)
            entry = std::make_unique<cell>(std::move(pieces.back()));
        else
        {
            entry->children.clear();
            for(HC &hc : pieces)
                entry->children.push_back(std::make_unique<cell>(std::move(hc)));
        }
    }

    void remove(link &entry, const slice &problem)
    {
        if(!entry->bounds.intersects(problem)) return;
        if(entry->children.empty())
        {
            --leaves;
            // Keep the old bounds only if subtraction creates an internal cell.
            replace(entry, entry->bounds.remove_slice_carefully(problem));
            return;
        }
        for(auto &child : entry->children) remove(child, problem);
        std::erase_if(entry->children, [](const link &child) { return !child; });
        if(entry->children.empty()) entry.reset();
        else compact(entry);
    }

    template<class Visitor>
    static bool visit(const cell &entry, const slice &restriction, Visitor &visitor)
    {
        if(!entry.bounds.intersects(restriction)) return false;
        if(entry.children.empty()) return visitor(entry.bounds);
        for(auto it = entry.children.rbegin(); it != entry.children.rend(); ++it)
            if(visit(**it, restriction, visitor)) return true;
        return false;
    }

public:
    explicit hc_tree(search_space space)
    {
        space.prune_empty();
        leaves = space.size();
        for(HC &hc : space) roots.push_back(std::make_unique<cell>(std::move(hc)));
    }

    bool empty() { finish_pending(); return leaves == 0; }
    std::size_t size() const noexcept { return leaves; }

    // Retain the selected leaf's bound until its replacement is known. Calls
    // must either replace it with concat/push_back or let empty() discard it.
    HC take_back()
    {
        finish_pending();
        assert(!roots.empty());
        link *entry = &roots.back();
        pending_path.push_back(entry);
        while(!(*entry)->children.empty())
        {
            entry = &(*entry)->children.back();
            pending_path.push_back(entry);
        }
        --leaves;
        return (*entry)->bounds;
    }

    void concat(search_space pieces)
    {
        assert(!pending_path.empty());
        replace(*pending_path.back(), std::move(pieces));
        // Replacement may be empty. Compact ancestors without deleting a
        // nonempty replacement, then forget all borrowed slot pointers.
        for(std::size_t i = pending_path.size() - 1; i > 0; --i)
        {
            auto &parent = *pending_path[i - 1];
            std::erase_if(parent->children, [](const link &child) { return !child; });
            if(parent->children.empty()) parent.reset();
            else compact(parent);
        }
        pending_path.clear();
        std::erase_if(roots, [](const link &root) { return !root; });
    }

    void push_back(HC hc)
    {
        search_space pieces;
        pieces.push_back(std::move(hc));
        concat(std::move(pieces));
    }

    void remove_slice_globally(const slice &problem)
    {
        finish_pending();
        for(auto &root : roots) remove(root, problem);
        std::erase_if(roots, [](const link &root) { return !root; });
    }

    // Traversal is read-only. The visitor must not mutate this tree; learned
    // conflicts are applied after traversal has returned.
    template<class Visitor>
    bool visit_intersections(const slice &restriction, Visitor visitor) const
    {
        assert(pending_path.empty());
        for(auto it = roots.rbegin(); it != roots.rend(); ++it)
            if(visit(**it, restriction, visitor)) return true;
        return false;
    }
};

#endif /* HC_TREE_H */
