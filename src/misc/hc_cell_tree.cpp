#include "hc_cell_tree.h"

hc_cell_tree::hc_cell_tree(search_space initial, hc_cell_options options)
    : settings(options)
{
    initial.prune_empty();
    leaves = initial.size();
    for(auto &domain : initial) roots.push_back(std::make_shared<cell>(std::move(domain)));
}

hc_cell_tree::~hc_cell_tree()
{
    if(auto session = traversal_session.lock()) session->owner = nullptr;
    destroying = true;
    for(auto &root : roots) discard(root);
}

hc_cell_tree::traversal::~traversal()
{
    if(owner) owner->finish();
}

std::shared_ptr<hc_cell_tree::traversal> hc_cell_tree::keep_traversal()
{
    auto session = traversal_session.lock();
    if(!session)
    {
        session = std::make_shared<traversal>(this);
        traversal_session = session;
    }
    return session;
}

void hc_cell_tree::dirty_chain(const link &entry)
{
    if(entry->chain_dirty) return;
    entry->chain_dirty = true;
    pending_chains.emplace_back(entry);
}

hc_cell_tree::link hc_cell_tree::resolve(link entry)
{
    auto target = entry;
    while(target && target->successor) target = target->successor;
    if(entry && entry->successor != target && entry->successor) entry->successor = target;
    return target && target->alive ? target : nullptr;
}

void hc_cell_tree::normalize(std::vector<link> &entries)
{
    for(auto &entry : entries) entry = resolve(std::move(entry));
    std::erase_if(entries, [](const link &entry) { return !entry; });
}

hc_cell_tree::link hc_cell_tree::prepare(link entry)
{
    entry = resolve(std::move(entry));
    if(!entry || (entry->prepared == revision && !entry->dirty)) return entry;
    auto ancestor = entry->parent.lock();
    if(auto parent = ancestor)
    {
        parent = prepare(std::move(parent));
        entry = resolve(std::move(entry));
        if(!entry) return nullptr;
        if(parent)
        {
            transfer(*parent);
            if(narrow(entry->bounds, parent->bounds)) entry->dirty = true;
        }
    }
    if(entry->bounds.empty())
    {
        leaves -= leaf_count(*entry);
        discard(entry);
        pending_compaction.emplace_back(ancestor);
        return nullptr;
    }
    if(!propagate(entry)) { pending_compaction.emplace_back(ancestor); return nullptr; }
    if(!entry->children.empty()) dirty_chain(entry);
    normalize(entry->children);
    if(entry->divided && entry->children.empty()) { discard(entry); return nullptr; }
    entry->prepared = revision;
    return entry;
}

void hc_cell_tree::compact_ancestors(link entry)
{
    while((entry = resolve(std::move(entry))))
    {
        auto parent = entry->parent.lock();
        normalize(entry->children);
        if(entry->children.empty() && entry->divided) discard(entry);
        else compact(entry);
        entry = std::move(parent);
    }
}

void hc_cell_tree::finish()
{
    for(const auto &entry : pending_compaction) compact_ancestors(entry.lock());
    pending_compaction.clear();
    normalize(roots);
    if(traversal_session.expired())
    {
        for(const auto &weak : pending_chains)
            if(auto entry = weak.lock())
            {
                link previous;
                for(const auto &child : entry->children)
                {
                    child->previous = previous;
                    previous = child;
                }
                entry->last_child = std::move(previous);
                entry->chain_dirty = false;
            }
        pending_chains.clear();
    }
}

hc_cell_tree::frontier hc_cell_tree::restrict(const HC &domain, const frontier *parent)
{
    frontier result;
    const auto append = [&](const auto &self, link entry) -> void {
        entry = prepare(std::move(entry));
        if(!entry || !intersects(entry->bounds, domain)) return;
        normalize(entry->children);
        if(!entry->children.empty())
        {
            const bool all_intersect = std::all_of(entry->children.begin(), entry->children.end(),
                [&](const link &child) { return intersects(child->bounds, domain); });
            if(!all_intersect)
            {
                transfer(*entry);
                for(const auto &child : entry->children) self(self, child);
                return;
            }
        }
        result.entries.push_back(std::move(entry));
    };
    for(const auto &entry : parent ? parent->entries : roots) append(append, entry);
    finish();
    return result;
}

integer_set hc_cell_tree::supported(frontier &candidates, const HC &restriction, index_t axis)
{
    integer_set result;
    const auto collect = [&](const auto &self, link entry, const integer_set &allowed) -> void {
        entry = prepare(std::move(entry));
        if(!entry || !intersects(entry->bounds, restriction)) return;
        auto values = entry->bounds[axis] & allowed;
        if(entry->failure != no_id)
        {
            const auto &failed = failures[entry->failure].region;
            bool covered = true;
            for(index_t n = 0; n < restriction.dimension(); ++n)
                if(n != axis && !entry->bounds[n].is_subset_of(failed[n])
                   && !restriction[n].is_subset_of(failed[n])
                   && !(entry->bounds[n] & restriction[n]).is_subset_of(failed[n]))
                { covered = false; break; }
            if(covered) values.minus(failed[axis]);
        }
        if(values.empty()) return;
        if(!entry->children.empty())
        {
            transfer(*entry);
            for(const auto &child : entry->children)
            {
                self(self, child, values);
                if(values.is_subset_of(result)) break;
            }
            return;
        }
        for(auto id : entry->negative)
        {
            const auto &fixed = problems[id].region.get_fixed_axes();
            bool covered = true;
            for(const auto &[n, excluded] : fixed)
                if(n != axis && !entry->bounds[n].is_subset_of(excluded)
                   && !restriction[n].is_subset_of(excluded)
                   && !(entry->bounds[n] & restriction[n]).is_subset_of(excluded))
                { covered = false; break; }
            if(!covered) continue;
            const auto cut = fixed.find(axis);
            if(cut == fixed.end()) return;
            values.minus(cut->second);
            if(values.empty()) return;
        }
        result |= values;
    };
    for(const auto &entry : candidates.entries)
    {
        collect(collect, entry, restriction[axis]);
        if(restriction[axis].is_subset_of(result)) break;
    }
    finish();
    normalize(candidates.entries);
    return result;
}

hc_cell_tree::discovery hc_cell_tree::discover(const frontier *candidates)
{
    discovery result;
    result.session = keep_traversal();
    for(const auto &entry : candidates ? candidates->entries : roots)
        result.push({entry, false});
    return result;
}

hc_cell_tree::discovery hc_cell_tree::discover(const handle &subtree)
{
    discovery result;
    result.session = keep_traversal();
    if(subtree.entry) result.push({subtree.entry, false});
    return result;
}

std::optional<hc_cell_tree::handle> hc_cell_tree::advance(
    discovery &cursor, const HC &restriction, std::size_t &credit, std::size_t &visits)
{
    while(credit && !cursor.empty())
    {
        auto &frame = *cursor.current;
        auto entry = frame.next;
        // Read the immutable sibling link before resolving unary successors.
        frame.next = frame.siblings ? entry->previous : nullptr;
        if(!frame.next) cursor.pop();
        --credit;
        ++visits;
        entry = prepare(std::move(entry));
        if(!entry || !intersects(entry->bounds, restriction) || failed_before(*entry, restriction)) continue;
        if(entry->children.empty())
        {
            finish();
            return handle(std::move(entry));
        }
        transfer(*entry);
        cursor.push({entry->last_child, true});
    }
    finish();
    return std::nullopt;
}

hc_cell_tree::leaf_view hc_cell_tree::read(const handle &leaf)
{
    auto entry = resolve(leaf.entry);
    assert(entry && entry->children.empty());
    leaf_view result{entry->bounds, {}};
    result.negative.reserve(entry->negative.size());
    for(auto id : entry->negative) result.negative.push_back(problems[id].region);
    return result;
}

void hc_cell_tree::exhausted(const handle &subtree, const HC &restriction, std::size_t work)
{
    if(auto entry = prepare(subtree.entry); entry && admit_failure(work))
        remember_failure(*entry, restriction);
    finish();
}

void hc_cell_tree::exhausted(const frontier *candidates, const HC &restriction, std::size_t work)
{
    if(admit_failure(work))
        for(const auto &entry : candidates ? candidates->entries : roots)
            if(auto current = prepare(entry)) remember_failure(*current, restriction);
    finish();
}

bool hc_cell_tree::narrow(HC &domain, const HC &parent)
{
    bool changed = false;
    for(std::size_t n = 0; n < domain.dimension(); ++n)
    {
        const auto previous = domain[n].size();
        domain[n] &= parent[n];
        changed |= domain[n].size() != previous;
    }
    return changed;
}

bool hc_cell_tree::intersects(const HC &domain, const HC &restriction)
{
    for(std::size_t n = 0; n < domain.dimension(); ++n)
        if(!domain[n].intersects(restriction[n])) return false;
    return true;
}

bool hc_cell_tree::restricted_subset(const HC &domain, const HC &restriction, const HC &superset)
{
    for(std::size_t n = 0; n < domain.dimension(); ++n)
        if(!domain[n].is_subset_of(superset[n])
           && !(domain[n] & restriction[n]).is_subset_of(superset[n])) return false;
    return true;
}

std::size_t hc_cell_tree::leaf_count(const cell &entry)
{
    if(entry.successor) return leaf_count(*entry.successor);
    if(!entry.alive) return 0;
    if(entry.children.empty()) return 1;
    std::size_t count = 0;
    for(const auto &child : entry.children) if(child) count += leaf_count(*child);
    return count;
}

void hc_cell_tree::reserve(std::vector<std::size_t> &buffer, std::size_t required)
{
    if(buffer.capacity() >= required) return;
    auto best = buffers.end();
    for(auto it = buffers.begin(); it != buffers.end(); ++it)
        if(it->capacity() >= required && (best == buffers.end() || it->capacity() < best->capacity()))
            best = it;
    if(best == buffers.end())
    {
        buffer.reserve(std::max(required, buffer.capacity() * 2));
        return;
    }
    auto replacement = std::move(*best);
    pooled_capacity -= replacement.capacity();
    buffers.erase(best);
    replacement.insert(replacement.end(), buffer.begin(), buffer.end());
    buffer.clear();
    buffer.swap(replacement);
    recycle(replacement);
}

void hc_cell_tree::recycle(std::vector<std::size_t> &buffer)
{
    buffer.clear();
    if(buffer.capacity() == 0) return;
    if(!destroying && buffers.size() < 16 && pooled_capacity + buffer.capacity() <= 8192)
    {
        pooled_capacity += buffer.capacity();
        buffers.push_back(std::move(buffer));
    }
    else std::vector<std::size_t>{}.swap(buffer);
}

void hc_cell_tree::acquire(cell &owner, std::size_t id)
{
    reserve(owner.negative, owner.negative.size() + 1);
    owner.negative.push_back(id);
    owner.dirty = true;
    owner.prepared = 0;
    ++problems[id].references;
    ++references;
}

void hc_cell_tree::release(std::size_t id)
{
    assert(problems[id].references > 0);
    --references;
    if(--problems[id].references == 0)
    {
        problems[id].region = slice{};
        free_problems.push_back(id);
        --live_problems;
    }
}

void hc_cell_tree::release_failure(cell &owner)
{
    if(owner.failure == no_id) return;
    auto &proof = failures[owner.failure];
    assert(proof.owner == &owner);
    proof.owner = nullptr;
    proof.region = HC{};
    owner.failure = no_id;
}

void hc_cell_tree::discard(link &entry)
{
    entry = resolve(std::move(entry));
    if(!entry) return;
    release_failure(*entry);
    for(auto id : entry->negative) release(id);
    recycle(entry->negative);
    for(auto &child : entry->children) discard(child);
    entry->children.clear();
    entry->last_child.reset();
    entry->bounds = HC{};
    entry->alive = false;
    entry.reset();
}

void hc_cell_tree::compact(link &entry)
{
    entry = resolve(std::move(entry));
    if(entry && !entry->children.empty()) dirty_chain(entry);
    if(entry) normalize(entry->children);
    while(entry && entry->children.size() == 1)
    {
        auto child = entry->children.front();
        if(narrow(child->bounds, entry->bounds)) child->dirty = true;
        if(!entry->negative.empty())
        {
            reserve(child->negative, child->negative.size() + entry->negative.size());
            child->negative.insert(child->negative.end(), entry->negative.begin(), entry->negative.end());
            entry->negative.clear();
            child->dirty = true;
        }
        recycle(entry->negative);
        if(entry->failure != no_id)
        {
            release_failure(*child);
            child->failure = entry->failure;
            failures[child->failure].owner = child.get();
            entry->failure = no_id;
        }
        child->parent = entry->parent;
        entry->children.clear();
        entry->last_child.reset();
        entry->bounds = HC{};
        entry->alive = false;
        entry->successor = child;
        entry = std::move(child);
        if(entry->bounds.empty())
        {
            leaves -= leaf_count(*entry);
            discard(entry);
        }
    }
}

void hc_cell_tree::replace(link &entry, search_space pieces)
{
    pieces.prune_empty();
    leaves += pieces.size();
    if(pieces.empty()) discard(entry);
    else if(pieces.size() == 1)
    {
        entry->bounds = std::move(pieces.back());
        entry->dirty = true;
    }
    else
    {
        entry->divided = true;
        for(auto &domain : pieces)
        {
            auto child = std::make_shared<cell>(std::move(domain));
            child->parent = entry;
            if(!entry->children.empty()) child->previous = entry->children.back();
            entry->children.push_back(std::move(child));
            entry->last_child = entry->children.back();
        }
    }
}

void hc_cell_tree::cut(link &entry, const slice &region)
{
    entry = resolve(std::move(entry));
    if(!entry) return;
    if(!entry->bounds.intersects(region)) return;
    if(entry->bounds.is_slice_good(region, 0))
    {
        leaves -= leaf_count(*entry);
        discard(entry);
        return;
    }
    if(entry->children.empty())
    {
        --leaves;
        replace(entry, entry->bounds.remove_slice_carefully(region));
        return;
    }
    for(auto &child : entry->children) cut(child, region);
    normalize(entry->children);
    if(entry->children.empty()) discard(entry);
    else compact(entry);
}

void hc_cell_tree::forget(cell &entry, std::size_t id)
{
    const auto it = std::find(entry.negative.begin(), entry.negative.end(), id);
    if(it != entry.negative.end()) { entry.negative.erase(it); release(id); }
    normalize(entry.children);
    for(auto &child : entry.children) forget(*child, id);
}

bool hc_cell_tree::propagate(link &entry)
{
    if(!entry->dirty) return true;
    bool changed;
    do
    {
        changed = false;
        for(std::size_t i = 0; i < entry->negative.size();)
        {
            const auto id = entry->negative[i];
            const auto &region = problems[id].region;
            if(!entry->bounds.intersects(region))
            {
                entry->negative.erase(entry->negative.begin() + i);
                release(id);
                continue;
            }
            if(!entry->bounds.is_slice_good(region)) { ++i; continue; }
            const auto &axes = region.get_fixed_axes();
            const auto axis = std::find_if(axes.begin(), axes.end(), [&](const auto &fixed) {
                return !entry->bounds[fixed.first].is_subset_of(fixed.second);
            });
            if(axis == axes.end())
            {
                leaves -= leaf_count(*entry);
                discard(entry);
                return false;
            }
            entry->bounds[axis->first].minus(axis->second);
            entry->negative.erase(entry->negative.begin() + i);
            release(id);
            changed = true;
        }
    } while(changed);
    entry->dirty = false;
    return true;
}

void hc_cell_tree::transfer(cell &entry)
{
    if(entry.negative.empty()) return;
    normalize(entry.children);
    cell *recipient = nullptr;
    for(auto &child : entry.children)
        if(child->negative.empty()) { recipient = child.get(); break; }
    for(auto &child : entry.children)
        if(child.get() != recipient)
            for(auto id : entry.negative)
                if(child->bounds.intersects(problems[id].region)) acquire(*child, id);
    if(recipient)
    {
        std::size_t out = 0;
        for(auto id : entry.negative)
            if(recipient->bounds.intersects(problems[id].region)) entry.negative[out++] = id;
            else release(id);
        entry.negative.resize(out);
        recycle(recipient->negative);
        recipient->negative = std::move(entry.negative);
        recipient->dirty = true;
    }
    else
    {
        for(auto id : entry.negative) release(id);
        recycle(entry.negative);
    }
}

bool hc_cell_tree::failed_before(const cell &entry, const HC &restriction) const
{
    return entry.failure != no_id
        && restricted_subset(entry.bounds, restriction, failures[entry.failure].region);
}

bool hc_cell_tree::admit_failure(std::size_t work) const
{
    if(settings.failure_capacity == 0 || settings.failures == hc_failure_policy::none) return false;
    if(settings.failures == hc_failure_policy::all) return true;
    // Binary branching bounds the number of cells by twice the leaf count.
    // If all cell records fit, broad admission cannot thrash the record arena.
    if(settings.failures == hc_failure_policy::adaptive && leaves <= settings.failure_capacity / 2) return true;
    return work >= settings.failure_work;
}

void hc_cell_tree::remember_failure(cell &entry, const HC &restriction)
{
    if(failed_before(entry, restriction)) return;
    auto id = entry.failure;
    if(id == no_id)
    {
        if(failures.capacity() == 0) failures.reserve(settings.failure_capacity);
        if(failures.size() < settings.failure_capacity)
        {
            id = failures.size();
            failures.push_back({nullptr, HC{}});
        }
        else
        {
            id = failure_cursor++ % settings.failure_capacity;
            if(failures[id].owner) failures[id].owner->failure = no_id;
        }
    }
    auto &proof = failures[id];
    proof.owner = &entry;
    proof.region = entry.bounds;
    narrow(proof.region, restriction);
    entry.failure = id;
}

void hc_cell_tree::add_negative(slice region)
{
    ++revision;
    normalize(roots);
    const auto id = free_problems.empty() ? problems.size() : free_problems.back();
    if(id == problems.size()) problems.push_back({std::move(region), 0});
    else { free_problems.pop_back(); problems[id].region = std::move(region); }
    ++live_problems;
    for(auto &root : roots) if(root->bounds.intersects(problems[id].region)) acquire(*root, id);
    if(problems[id].references == 0)
    {
        problems[id].region = slice{};
        free_problems.push_back(id);
        --live_problems;
    }
}

void hc_cell_tree::remove_slice_globally(const slice &region)
{
    ++revision;
    for(auto &root : roots) cut(root, region);
    normalize(roots);
}

void hc_cell_tree::balance(const HC &universe)
{
    if(live_problems <= leaves) return;
    const auto codimension = [&](const slice &region) {
        std::size_t result = 0;
        for(const auto &[axis, values] : region.get_fixed_axes()) result += !universe[axis].is_subset_of(values);
        return result;
    };
    auto best = no_id;
    for(std::size_t id = 0; id < problems.size(); ++id)
        if(problems[id].references && (best == no_id || codimension(problems[id].region) < codimension(problems[best].region))) best = id;
    if(best == no_id) return;
    const auto region = problems[best].region;
    remove_slice_globally(region);
    for(auto &root : roots) forget(*root, best);
}

hc_cell_tree::statistics hc_cell_tree::inspect() const
{
    statistics result;
    result.leaves = leaves;
    result.problems = live_problems;
    result.references = references;
    result.reference_capacity = pooled_capacity;
    const auto visit = [&](const auto &self, const cell &entry) -> void {
        if(entry.successor) { self(self, *entry.successor); return; }
        if(!entry.alive) return;
        ++result.cells;
        result.reference_capacity += entry.negative.capacity();
        for(const auto &child : entry.children) self(self, *child);
    };
    for(const auto &root : roots) if(auto entry = resolve(root)) visit(visit, *entry);
    for(const auto &proof : failures) result.cached_failures += proof.owner != nullptr;
    return result;
}
