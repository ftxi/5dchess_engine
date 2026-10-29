#include <cassert>
#include <sstream>

#include "finetree.h"

template<typename T>
    requires std::default_initializable<T>
inline fine_node<T>::fine_node(
    fine_node *parent,
    const state &s,
    T info_value, fine_search_options options)
: parent{parent}, pocessed_context{nullptr}, context{nullptr}, n{index_t(-7)}, i{index_t(-7)}, info{std::move(info_value)}
{
    ignite_impl(s, options);
}

template<typename T>
    requires std::default_initializable<T>
inline fine_node<T>::fine_node(fine_node *parent, index_t n, index_t i, T info_value)
: parent{parent}, pocessed_context{nullptr}, context{parent->get_context()}, n{n}, i{i}, info{std::move(info_value)}
{
    if(n + 1 < context->hc_info.dimension())
    {
        const auto axis = n + 1;
        const auto *parent_query = parent->query();
        const auto *base = parent->completion ? parent->completion->base.get() : nullptr;
        if(n >= context->hc_info.first_branch_axis() && i == 0) remaining = integer_set{0};
        else if(parent_query && parent_query->active[n].contains(i))
            remaining = parent_query->active[axis];
        else if(base) remaining = base->domain[axis];
        else remaining = context->hc_info.get_universe()[axis];
    }
}

template<typename T>
    requires std::default_initializable<T>
inline fine_node<T>::fine_node(fine_node *parent, index_t n, index_t i)
: fine_node(parent, n, i, T{}) {}

template<typename T>
    requires std::default_initializable<T>
inline std::unique_ptr<fine_node<T>> fine_node<T>::make_root(
    const state &s,
    T info, fine_search_options options)
{
    return std::unique_ptr<fine_node<T>>(
        new fine_node(nullptr, s, std::move(info), options));
}

template<typename T>
    requires std::default_initializable<T>
inline std::unique_ptr<fine_node<T>> fine_node<T>::make_temproary(fine_node *parent, index_t n, index_t i, T info)
{
    return std::unique_ptr<fine_node<T>>(new fine_node(parent, n, i, std::move(info)));
}

template<typename T>
    requires std::default_initializable<T>
inline fine_node<T> *fine_node<T>::add_child(index_t n, index_t i)
{
    assert(!get_child(i) && "fine-tree siblings must have unique coordinates");
    auto ctx = get_context();
    ctx->node_pool.emplace_back(this, n, i);
    fine_node<T> *child = &ctx->node_pool.back();
    children.push_back(child);
    return child;
}

template<typename T>
    requires std::default_initializable<T>
inline bool fine_node<T>::is_ceiling() const noexcept
{
    if(!context)
    {
        // root node
        return false;
    }
    // is_ceiling needs the older context, not the newer one
    return n + 1 == context->hc_info.dimension();
}

template <typename T>
    requires std::default_initializable<T>
template <HCOrdering Order>
inline bool fine_node<T>::is_terminal(const Order &order) &
{
    if(!is_nodal())
    {
        // if the node is not nodal, it is not terminal because every temporary node has a valid branch as witness
        return false;
    }
    else if(pocessed_context->verified_terminal)
    {
        // No legal action was found in the complete nodal restriction.
        return true;
    }
    if(!get_children().empty())
    {
        // if the node already has children, it is not terminal
        return false;
    }
    if(expand_next(order))
    {
        // if the node has a valid search result, it is not terminal
        return false;
    }
    pocessed_context->verified_terminal = true;
    return true;
}

template<typename T>
    requires std::default_initializable<T>
inline bool fine_node<T>::is_terminal() &
{
    return is_terminal(natural_HC_ordering{});
}

template<typename T>
    requires std::default_initializable<T>
inline fine_node<T> *fine_node<T>::get_child(index_t i) & noexcept
{
    for(fine_node<T> *next_node : children)
    {
        if(next_node->i == i)
        {
            return next_node;
        }
    }
    return nullptr;
}

template<typename T>
    requires std::default_initializable<T>
inline const fine_node<T> *fine_node<T>::get_child(index_t i) const & noexcept
{
    for(const fine_node<T> *next_node : children)
    {
        if(next_node->i == i)
        {
            return next_node;
        }
    }
    return nullptr;
}

template<typename T>
    requires std::default_initializable<T>
inline typename fine_node<T>::nodal_pocession *fine_node<T>::get_context() & noexcept
{
    return pocessed_context ? pocessed_context.get() : context;
}

template<typename T>
    requires std::default_initializable<T>
inline const HC_info &fine_node<T>::get_hc_info() const & noexcept
{
    return pocessed_context ? pocessed_context->hc_info : context->hc_info;
}

template<typename T>
    requires std::default_initializable<T>
inline bool fine_node<T>::get_player() const noexcept
{
    return get_hc_info().get_state().get_present().c();
}

template<typename T>
    requires std::default_initializable<T>
inline std::string fine_node<T>::print_semimove() const
{
    if(context)
    {
        return context->hc_info.get_semimove(n, i).to_string(context->hc_info.get_state());
    }
    else
    {
        return "root";
    }
}

template<typename T>
    requires std::default_initializable<T>
template<HCOrdering Order>
inline generator<index_t> fine_node<T>::search(Order order) &
{
    fine_node<T> *next_node = expand_next(order);
    while(next_node)
    {
        co_yield next_node->i;
        next_node = expand_next(order);
    }
}

template<typename T>
    requires std::default_initializable<T>
template<HCOrdering Order>
inline fine_node<T> *fine_node<T>::expand_one(const Order &order)
{
    auto *ctx = get_context();
    auto *continuation = query();
    const auto *inherited = completion ? completion->base.get() : nullptr;
    const index_t axis = is_nodal() ? 0 : n + 1;
    if(axis >= ctx->hc_info.dimension() || remaining.empty()) return nullptr;
    const HC *propagated = nullptr;
    const auto admit = [&](index_t coordinate, std::shared_ptr<const completion_record> witness) {
        auto *child = add_child(axis, coordinate);
        if(propagated && axis + 1 < ctx->hc_info.dimension()) child->remaining &= (*propagated)[axis + 1];
        if(!child->is_ceiling())
        {
            child->completion = witness;
            const auto next_axis = axis + 1;
            // Existing axes fix all departures and existing arrivals. Their
            // unmatched departures determine the number of branch arrivals;
            // occupied branch axes form a prefix, so its null pattern is fixed.
            if(next_axis >= ctx->hc_info.first_branch_axis())
            {
                if(witness->coordinates[next_axis] == 0) child->remaining = integer_set{0};
                else child->remaining.erase(0);
            }
            else if(ctx->hc_info.is_departure(next_axis, witness->coordinates[next_axis]))
                for(index_t prefix = 0; prefix <= axis; ++prefix)
                    if(auto partner = ctx->hc_info.arrival_partner(prefix, witness->coordinates[prefix]);
                       partner && partner->first == next_axis)
                    { child->remaining = integer_set{partner->second}; break; }
        }
        remaining.erase(coordinate);
        if(remaining.empty())
        {
            remaining = integer_set{};
            if(pending) pending->global.reset();
        }
        completion = remaining.empty() ? nullptr : std::move(witness);
        return child;
    };
    if(completion)
    {
        index_t first;
        {
            auto values = order(axis, remaining);
            if(values.begin() == values.end()) return nullptr;
            first = *values.begin();
        }
        // A legal completion already certifies the first requested coordinate.
        // In particular, forced/null suffixes need no HC construction or solver.
        if(first == completion->coordinates[axis]) return admit(first, completion);
        if(auto candidate = ctx->hc_info.try_local_completion(completion->coordinates, axis, first))
            return admit(first, std::make_shared<const completion_record>(std::move(*candidate),
                continuation ? pending->global : completion->base));
        const auto *local_parent = !is_nodal() ? (parent->pending ? parent->pending->local.get() : nullptr) : nullptr;
        if(auto candidate = ctx->hc_info.resume_local(ctx->search, pending_search().local,
                local_parent, completion->coordinates, axis, first, order))
            return admit(first, std::make_shared<const completion_record>(std::move(*candidate),
                continuation ? pending->global : completion->base));
    }
    if(!continuation)
    {
        HC domain = inherited ? inherited->domain : ctx->hc_info.get_universe();
        const auto depth = inherited ? inherited->prefix_length : 0;
        for(auto *ancestor = this; !ancestor->is_nodal() && ancestor->n >= depth; ancestor = ancestor->parent)
            domain[ancestor->n] = integer_set{ancestor->i};
        domain[axis] &= remaining;
        pending_search().global = std::make_shared<HC_info::completion_query>(
            ctx->hc_info.restrict_completion(ctx->search, std::move(domain), axis, inherited));
        continuation = query();
    }
    const bool changed = !continuation->active[axis].is_subset_of(remaining);
    continuation->active[axis] &= remaining;
    if(changed) continuation->active[axis] &= ctx->search.cells.supported(
        continuation->choices, continuation->active, axis);
    if(changed && !ctx->hc_info.constrain_domain(continuation->active, &ctx->search.incremental))
    { remaining = integer_set{}; completion.reset(); if(pending) pending->global.reset(); return nullptr; }
    remaining &= continuation->active[axis];
    if(changed) continuation->choices = ctx->search.cells.restrict(continuation->active, &continuation->choices);
    if(remaining.empty() || continuation->choices.empty())
    {
        remaining = integer_set{};
        completion.reset();
        if(pending) pending->global.reset();
        return nullptr;
    }
    HC &restriction = ctx->restriction;
    restriction = continuation->active;
    propagated = &continuation->active;
    if(!completion || !restriction.contains(completion->coordinates))
    {
        // A single unrestricted failure can close a terminal prefix. Do not
        // repeat an expensive matching failure separately for every coordinate.
        auto candidate = ctx->hc_info.find_witness(ctx->search, restriction, order, continuation);
        if(!candidate)
        { remaining = integer_set{}; completion.reset(); if(pending) pending->global.reset(); return nullptr; }
        completion = std::make_shared<const completion_record>(std::move(*candidate), pending->global);
    }
    integer_set grouped_attempts;
    while(!remaining.empty())
    {
        index_t coordinate;
        {
            // HCOrdering ranges borrow their mask; end that borrow before
            // erasing admitted or impossible coordinates from remaining.
            auto values = order(axis, remaining);
            if(values.begin() == values.end()) return nullptr;
            coordinate = *values.begin();
        }
        restriction[axis] = integer_set{coordinate};
        auto witness = completion;
        if(!witness || !restriction.contains(witness->coordinates))
        {
            auto candidate = ctx->hc_info.find_witness(ctx->search, restriction, order, continuation);
            if(!candidate)
            {
                remaining.erase(coordinate);
                restriction[axis] = remaining;
                remaining &= ctx->search.cells.supported(continuation->choices, restriction, axis);
                // When many earlier choices share a move kind that is absent
                // from the retained witness, one complete grouped query can
                // prove them all impossible. A successful group query is only
                // a witness; it never skips an earlier ordered coordinate.
                auto group = ctx->hc_info.same_move_kind(axis, coordinate, remaining);
                if(group.size() >= 8 && !group.is_subset_of(grouped_attempts)
                   && (!completion || !group.contains(completion->coordinates[axis])))
                {
                    grouped_attempts |= group;
                    restriction[axis] = group;
                    auto grouped = ctx->hc_info.find_witness(ctx->search, restriction, order, continuation);
                    if(grouped) completion = std::make_shared<const completion_record>(std::move(*grouped), pending->global);
                    else remaining.minus(group);
                }
                restriction[axis] = remaining;
                continue;
            }
            witness = std::make_shared<const completion_record>(std::move(*candidate), pending->global);
        }
        return admit(coordinate, std::move(witness));
    }
    remaining = integer_set{};
    completion.reset();
    if(pending) pending->global.reset();
    return nullptr;
}

template<typename T>
    requires std::default_initializable<T>
template<HCOrdering Order>
inline fine_node<T> *fine_node<T>::expand_next(const Order &order) &
{
    auto *first = expand_one(order);
    if(!first) return nullptr;
    // Preserve MCTS's complete-witness-branch contract. Every suffix coordinate
    // is selected by the same order; a matching witness never overrides it.
    auto *current = first;
    while(!current->is_ceiling())
    {
        current = current->expand_one(order);
        assert(current && "an admitted prefix must retain a legal completion");
    }
    return first;
}

template<typename T>
    requires std::default_initializable<T>
inline void fine_node<T>::ignite() &
{
    if(is_nodal()) return;
    assert(is_ceiling());
    const auto options = context->options;
    state s = context->hc_info.get_state();
    moveseq mvs = to_action();
    for(full_move mv : mvs)
    {
        [[maybe_unused]] const bool applied = s.apply_move<true>(mv);
        assert(applied);
    }
    s.submit<true>();
    ignite_impl(s, options);
}

template<typename T>
    requires std::default_initializable<T>
inline void fine_node<T>::ignite_impl(const state &s, fine_search_options options)
{
    if(options.builder == fine_builder::solid)
    {
        auto [info, domain, negative] = HC_info::build_solid_HC(s);
        pocessed_context = std::make_unique<nodal_pocession>(
            std::move(info), search_space{std::move(domain)}, options);
        for(auto &problem : negative) pocessed_context->search.cells.add_negative(std::move(problem));
    }
    else
    {
        auto [info, initial] = HC_info::build_HC(s);
        pocessed_context = std::make_unique<nodal_pocession>(std::move(info), std::move(initial), options);
    }
    remaining = pocessed_context->hc_info.dimension() ? pocessed_context->hc_info.get_universe()[0] : integer_set{};
    completion.reset();
    pending.reset();
}

template<typename T>
    requires std::default_initializable<T>
inline const fine_search_options &fine_node<T>::get_search_options() const noexcept
{
    return (pocessed_context ? pocessed_context.get() : context)->options;
}

template<typename T>
    requires std::default_initializable<T>
inline typename fine_node<T>::statistics fine_node<T>::search_statistics() const
{
    const auto *ctx = pocessed_context ? pocessed_context.get() : context;
    return {ctx->search.cells.inspect(), ctx->node_pool.size() + 1,
        ctx->search.scratch.matching_work(), ctx->search.witness_calls,
        ctx->search.local_hits, ctx->search.forest_searches,
        ctx->search.local_resumes, ctx->search.local_resume_hits};
}

template<typename T>
    requires std::default_initializable<T>
inline moveseq fine_node<T>::to_action() const
{
    assert(is_ceiling());
    int dim = context->hc_info.dimension();
    std::vector<index_t> pt(dim);
    auto current = this;
    // Walk parent pointers to build the point vector.
    // Loop while current has a parent (i.e., not the root) so this works
    // even if the ceiling node has been ignited (is_nodal() == true).
    for(int j = 0; j < dim; j++)
    {
        assert((j==0 || !current->is_nodal()) && "expected more temproary nodes");
        pt[current->n] = current->i;
        current = current->parent;
        assert(current && "expected more nodes before root");
    }
    assert(current->is_nodal());
    return context->hc_info.to_action(pt);
}

template<typename T>
    requires std::default_initializable<T>
inline std::string fine_node<T>::to_string() const
{
    std::ostringstream oss;

    const auto dump_node = [&](const fine_node<T> &node, size_t depth, const auto &self) -> void {
        const std::string indent(depth * 2, ' ');

        oss << indent << "fine_node{";
        if(node.is_nodal())
        {
            oss << "nodal";
        }
        else
        {
            oss << "temporary n=" << node.n << ", i=" << node.i;
        }
        oss << ", remaining=" << node.remaining.to_string() << "}\n";

        oss << indent << "  children#=" << node.children.size() << "\n";
        for(size_t child_index = 0; child_index < node.children.size(); ++child_index)
        {
            oss << indent << "    child[" << child_index << "]\n";
            self(*node.children[child_index], depth + 4, self);
        }
    };

    dump_node(*this, 0, dump_node);
    return oss.str();
}
