#ifndef HC_CELL_TREE_H
#define HC_CELL_TREE_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include "geometry.h"

enum class hc_failure_policy { none, all, expensive, adaptive };

struct hc_cell_options
{
    hc_failure_policy failures = hc_failure_policy::adaptive;
    std::size_t failure_capacity = 1024;
    std::size_t failure_work = 16;
};

// Shared candidate space for restricted witness searches. Children partition
// surviving space; inherited bounds are materialized lazily during traversal.
// Restriction-dependent failures never remove space from another restriction.
class hc_cell_tree
{
    static constexpr std::size_t no_id = std::size_t(-1);
    struct cell;
    using link = std::shared_ptr<cell>;
    struct cell
    {
        HC bounds;
        std::vector<link> children;
        std::weak_ptr<cell> parent;
        link successor;
        // Stable reverse sibling chain for suspended DFS, including dead cells.
        link previous;
        link last_child;
        std::vector<std::size_t> negative;
        std::size_t failure = no_id;
        std::size_t prepared = 0;
        bool alive = true;
        bool divided = false;
        bool dirty = true;
        bool chain_dirty = false;
        explicit cell(HC domain) : bounds(std::move(domain)) {}
    };
    struct problem { slice region; std::size_t references = 0; };
    struct failure { cell *owner; HC region; };
    std::vector<link> roots;
    std::vector<problem> problems;
    std::vector<std::size_t> free_problems;
    std::vector<failure> failures;
    std::size_t failure_cursor = 0;
    std::size_t leaves = 0, live_problems = 0, references = 0;
    std::size_t revision = 1;
    bool destroying = false;
    hc_cell_options settings;
    std::vector<std::vector<std::size_t>> buffers;
    std::size_t pooled_capacity = 0;
    std::vector<const slice *> slice_refs;
    std::vector<std::weak_ptr<cell>> pending_compaction;
    struct traversal
    {
        hc_cell_tree *owner;
        explicit traversal(hc_cell_tree *owner) : owner(owner) {}
        ~traversal();
    };
    std::weak_ptr<traversal> traversal_session;
    std::vector<std::weak_ptr<cell>> pending_chains;
    std::shared_ptr<traversal> keep_traversal();
    void dirty_chain(const link &entry);

    static bool narrow(HC &domain, const HC &parent);
    static bool intersects(const HC &domain, const HC &restriction);
    static bool restricted_subset(const HC &domain, const HC &restriction, const HC &superset);
    static std::size_t leaf_count(const cell &entry);
    static link resolve(link entry);
    static void normalize(std::vector<link> &entries);
    link prepare(link entry);
    void compact_ancestors(link entry);
    void finish();
    void reserve(std::vector<std::size_t> &buffer, std::size_t required);
    void recycle(std::vector<std::size_t> &buffer);
    void acquire(cell &owner, std::size_t id);
    void release(std::size_t id);
    void release_failure(cell &owner);
    void discard(link &entry);
    void compact(link &entry);
    void replace(link &entry, search_space pieces);
    void cut(link &entry, const slice &region);
    void forget(cell &entry, std::size_t id);
    bool propagate(link &entry);
    void transfer(cell &entry);
    bool failed_before(const cell &entry, const HC &restriction) const;
    void remember_failure(cell &entry, const HC &restriction);
    bool admit_failure(std::size_t work) const;

    template<class Projector>
    integer_set project_cell(link entry, const HC &restriction, index_t axis, Projector &projector, bool complete)
    {
        entry = prepare(std::move(entry));
        if(!entry || !intersects(entry->bounds, restriction) || failed_before(*entry, restriction)) return {};
        integer_set result;
        if(entry->children.empty())
        {
            slice_refs.clear();
            for(auto id : entry->negative) slice_refs.push_back(&problems[id].region);
            result = projector(entry->bounds, std::span<const slice *const>(slice_refs));
        }
        else
        {
            transfer(*entry);
            for(const auto &child : entry->children)
            {
                result |= project_cell(child, restriction, axis, projector, complete);
                if(!complete && restriction[axis].is_subset_of(result)) break;
            }
        }
        if(result.empty() && admit_failure(0)) remember_failure(*entry, restriction);
        return result;
    }

    template<class Visitor>
    bool visit_cell(link &entry, const HC *parent, const HC &restriction,
                    Visitor &visitor, const std::size_t &work)
    {
        entry = prepare(entry);
        if(!entry) return false;
        if(parent && narrow(entry->bounds, *parent)) entry->dirty = true;
        if(entry->bounds.empty())
        {
            leaves -= leaf_count(*entry);
            discard(entry);
            return false;
        }
        if(!intersects(entry->bounds, restriction)) return false;
        if(failed_before(*entry, restriction)) return false;
        const auto started = work;
        if(!propagate(entry)) return false;
        entry->prepared = revision;
        if(!intersects(entry->bounds, restriction)) return false;
        bool found = false;
        if(entry->children.empty())
        {
            slice_refs.clear();
            for(auto id : entry->negative) slice_refs.push_back(&problems[id].region);
            // The visitor must publish any learned problem after returning.
            found = visitor(entry->bounds, std::span<const slice *const>(slice_refs));
        }
        else
        {
            transfer(*entry);
            for(auto it = entry->children.rbegin(); it != entry->children.rend(); ++it)
                if(visit_cell(*it, &entry->bounds, restriction, visitor, work))
                {
                    found = true;
                    break;
                }
            normalize(entry->children);
            if(entry->children.empty()) { discard(entry); return false; }
            compact(entry);
        }
        if(entry && !found && admit_failure(work - started))
            remember_failure(*entry, restriction);
        return found;
    }

public:
    struct handle
    {
    private:
        friend class hc_cell_tree;
        link entry;
        explicit handle(link entry) : entry(std::move(entry)) {}
    public:
        handle() = default;
    };
    struct discovery
    {
    private:
        friend class hc_cell_tree;
        struct frame { link next; bool siblings; };
        std::vector<frame> pending;
        std::optional<frame> current;
        std::shared_ptr<traversal> session;
        void push(frame entry)
        {
            if(current) pending.push_back(std::move(*current));
            current = std::move(entry);
        }
        void pop()
        {
            current.reset();
            if(pending.empty()) return;
            current = std::move(pending.back());
            pending.pop_back();
        }
    public:
        bool empty() const noexcept { return !current; }
        std::size_t storage() const noexcept { return sizeof(*this) + pending.capacity() * sizeof(frame); }
    };
    struct leaf_view
    {
        HC bounds;
        std::vector<slice> negative;
    };
    // An antichain of candidate subtrees. Handles survive cuts and unary
    // collapse; reclaimed cells retain no domain or negative storage.
    struct frontier
    {
    private:
        friend class hc_cell_tree;
        std::vector<link> entries;
    public:
        bool empty() const noexcept { return entries.empty(); }
    };
    explicit hc_cell_tree(search_space initial, hc_cell_options options = {});
    ~hc_cell_tree();
    hc_cell_tree(const hc_cell_tree &) = delete;
    hc_cell_tree &operator=(const hc_cell_tree &) = delete;
    hc_cell_tree(hc_cell_tree &&) = default;
    hc_cell_tree &operator=(hc_cell_tree &&) = delete;

    bool empty() const noexcept { return leaves == 0; }
    std::size_t size() const noexcept { return leaves; }
    void add_negative(slice region);
    void balance(const HC &universe);
    void remove_slice_globally(const slice &region);
    frontier restrict(const HC &domain, const frontier *parent = nullptr);
    // Reverse DFS charges one pending node per unit and admits at most one
    // leaf. An empty return can mean yield; inspect cursor.empty() for proof
    // of exhaustion. No failure is recorded by admission or suspension.
    discovery discover(const frontier *candidates = nullptr);
    discovery discover(const handle &subtree);
    std::optional<handle> advance(discovery &cursor, const HC &restriction,
                                  std::size_t &credit, std::size_t &visits);
    // Read immediately after admission, before another tree mutation. The
    // snapshot owns its exclusions, including any recycled problem records.
    leaf_view read(const handle &leaf);
    // Call only after proving exhaustion of the entire restricted subtree.
    void exhausted(const handle &subtree, const HC &restriction, std::size_t work);
    void exhausted(const frontier *candidates, const HC &restriction, std::size_t work);
    std::size_t version() const noexcept { return revision; }
    integer_set supported(frontier &candidates, const HC &restriction, index_t axis);

    // The projector returns an overapproximation of supported coordinates.
    // An empty projection proves failure under this restriction.
    template<class Projector>
    integer_set project(frontier &candidates, const HC &restriction, index_t axis,
                        Projector projector, bool complete = false)
    {
        integer_set result;
        for(const auto &entry : candidates.entries)
        {
            result |= project_cell(entry, restriction, axis, projector, complete);
            if(!complete && restriction[axis].is_subset_of(result)) break;
        }
        finish();
        normalize(candidates.entries);
        return result;
    }

    template<class Visitor>
    bool visit(frontier &candidates, const HC &restriction, Visitor visitor, const std::size_t &work)
    {
        bool found = false;
        for(auto it = candidates.entries.rbegin(); it != candidates.entries.rend(); ++it)
        {
            auto entry = prepare(*it);
            if(!entry) { it->reset(); continue; }
            auto parent = entry->parent.lock();
            found = visit_cell(entry, nullptr, restriction, visitor, work);
            *it = std::move(entry);
            compact_ancestors(std::move(parent));
            if(found) break;
        }
        normalize(candidates.entries);
        finish();
        return found;
    }

    template<class Visitor>
    bool visit(const HC &restriction, Visitor visitor, const std::size_t &work)
    {
        bool found = false;
        for(auto it = roots.rbegin(); it != roots.rend(); ++it)
            if(visit_cell(*it, nullptr, restriction, visitor, work))
            {
                found = true;
                break;
            }
        finish();
        return found;
    }

    struct statistics
    {
        std::size_t cells = 0, leaves = 0, problems = 0, references = 0;
        std::size_t cached_failures = 0, reference_capacity = 0;
    };
    statistics inspect() const;
};

#endif /* HC_CELL_TREE_H */
