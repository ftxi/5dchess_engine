#include <cassert>
#include <sstream>
#include <map>

#include "finetree.h"

template<typename T>
inline fine_node<T>::fine_node(
    fine_node *parent,
    state s,
    T info_value,
    fine_tree_options options)
: parent{parent}, pocessed_context{nullptr}, context{nullptr}, n{index_t(-7)}, i{index_t(-7)}, cells{}, info{std::move(info_value)}
{
    auto [hc_info, ss] = HC_info::build_HC(s);
    HC universe = hc_info.universe;
    pocessed_context = std::make_unique<nodal_pocession<T>>(nodal_pocession<T>{
        .hc_info = std::move(hc_info),
        .node_pool = {},
        .cell_pool = {
            fine_cell<T>{
                .parent = nullptr,
                .node = this,
                .space = std::move(universe),
                .children = {},
                .subspace = std::move(ss)
            }
        },
        .options = options,
        .verified_terminal = false
    });
    cells.push_back(&pocessed_context->cell_pool.back());
}

template<typename T>
inline fine_node<T>::fine_node(fine_node *parent, index_t n, index_t i, T info_value)
: parent{parent}, pocessed_context{nullptr}, context{parent->get_context()}, n{n}, i{i}, cells{}, info{std::move(info_value)} {}

template<typename T>
inline std::unique_ptr<fine_node<T>> fine_node<T>::make_root(
    state s,
    T info,
    fine_tree_options options)
{
    return std::unique_ptr<fine_node<T>>(
        new fine_node(nullptr, s, std::move(info), options));
}

template<typename T>
inline std::unique_ptr<fine_node<T>> fine_node<T>::make_temproary(fine_node *parent, index_t n, index_t i, T info)
{
    return std::unique_ptr<fine_node<T>>(new fine_node(parent, n, i, std::move(info)));
}

template<typename T>
inline fine_cell<T> *fine_node<T>::add_cell(fine_cell<T> &&cell)
{
    auto ctx = get_context();
    ctx->cell_pool.push_back(std::move(cell));
    fine_cell<T> *new_cell = &ctx->cell_pool.back();
    cells.push_back(new_cell);
    new_cell->parent->children.push_back(new_cell);
    return new_cell;
}

// TODO: insert elements while keep the increasing order of i
template<typename T>
inline fine_node<T> *fine_node<T>::add_child(index_t n, index_t i, T info)
{
    auto ctx = get_context();
    ctx->node_pool.emplace_back(this, n, i, std::move(info));
    fine_node<T> *child = &ctx->node_pool.back();
    children.push_back(child);
    return child;
}

template<typename T>
inline bool fine_node<T>::is_ceiling() const
{
    if(!context)
    {
        // root node
        return false;
    }
    // is_ceiling needs the older context, not the newer one
    return n + 1 == context->hc_info.dimension;
}

template<typename T>
inline fine_node<T> *fine_node<T>::get_nearby_ceiling()
{
    fine_node<T> *current = this;
    if(current->is_terminal())
    {
        // there is no ceiling because node is already terminal
        return nullptr;
    }
    while (!current->is_ceiling())
    {
        // otherwise, our construction gurantees a path to a ceiling node
        assert(!current->get_children().empty() && "non-terminal node should have witness");
        current = current->children[0];
    }
    return current;
}

template <typename T>
inline bool fine_node<T>::is_terminal()
{
    if(!is_nodal())
    {
        // if the node is not nodal, it is not terminal because every temporary node has a valid branch as witness
        return false;
    }
    else if(pocessed_context->verified_terminal)
    {
        // if it is already verified to be terminal, return true
        return true;
    }
    if(!get_children().empty())
    {
        // if the node already has children, it is not terminal
        return false;
    }
    if(auto i_opt = search().first())
    {
        // if the node has a valid search result, it is not terminal
        return false;
    }
    pocessed_context->verified_terminal = true;
    return true;
}

template <typename T>
inline fine_node<T> *fine_node<T>::get_child(index_t i) const
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
inline nodal_pocession<T> *fine_node<T>::get_context() const
{
    if(pocessed_context)
    {
        return pocessed_context.get();
    }
    else
    {
        return context;
    }
}

template <typename T>
inline std::string fine_node<T>::print_semimove() const
{
    if(context)
    {
        semimove &sm = context->hc_info.axis_coords[n][i];
        return show_semimove(sm);
    }
    else
    {
        return "root";
    }
}

template<typename T>
inline generator<index_t> fine_node<T>::search()
{
    fine_node<T> *next_node = expand();
    while(next_node)
    {
        co_yield next_node->i;
        next_node = expand();
    }
}

template <typename T>
inline bool fine_node<T>::gen_all_children()
{
    if(is_ceiling() && !is_nodal())
    {
        ignite();
    }
    for(auto i : search())
    {
        (void)i; // ignore the value, just generate all children
    }
    if(is_nodal())
    {
        bool is_terminal = get_children().empty();
        pocessed_context->verified_terminal = is_terminal;
        return !is_terminal;
    }
    else
    {
        return false;
    }
}

template<typename T>
inline fine_node<T> *fine_node<T>::expand()
{
    auto ans = explore();
    if(!ans)
    {
        return nullptr;
    }
    auto [pt, cell, hc] = *ans;
    fine_node<T> *final_node = isolate(pt, cell, hc);
    fine_node<T> *next_node = normalize(pt, cell, final_node);
    return next_node;
}

template<typename T>
inline std::optional<std::tuple<point, fine_cell<T> *, HC *>> fine_node<T>::explore()
{
    HC_info &hc_info = get_context()->hc_info;
    for(fine_cell<T> *cell : cells)
    {
        // search for each cell in cells
        while(!cell->subspace.empty())
        {
            // while the search space of this cell is not exhausted
            HC &hc = cell->subspace.back();
            //cell.subspace.hcs.pop_back();
            // try to take a point in this hc
            auto pt_opt = hc_info.take_point(hc);
            if(pt_opt)
            {
                auto problem = hc_info.find_problem(*pt_opt, hc);
                if(problem)
                {
                    // if there is a problem with this point
                    // remove the problem for all relevant cells
                    remove_problem(*problem, cell);
                }
                else
                {
                    // otherwise we are done
                    return std::optional<std::tuple<point, fine_cell<T> *, HC *>>{std::in_place, *pt_opt, cell, &hc};
                }
            }
            else
            {
                // if there is no more point, remove this hc
                cell->subspace.pop_back();
            }
        }
    }
    // explored all cells without finding a solution; report failure
    return std::nullopt;
}

template<typename T>
inline void fine_node<T>::remove_from_cell(
    const slice &s,
    fine_cell<T> *cell,
    bool force_back_removal)
{
    if(cell->space.intersects(s))
    {
        cell->subspace.remove_slice_backwards(
            s,
            get_context()->options.scan_policy.disjoint_weight,
            force_back_removal,
            get_context()->options.quality_policy.max_codim);
    }
}

template<typename T>
inline void fine_node<T>::remove_from_node(
    const slice &s,
    fine_node<T> *node,
    fine_cell<T> *preferred_cell,
    bool force_preferred_removal)
{
    if(preferred_cell != nullptr)
    {
        remove_from_cell(
            s, preferred_cell, force_preferred_removal);
    }
    for(fine_cell<T> *cell : node->cells)
    {
        if(cell != preferred_cell)
        {
            remove_from_cell(s, cell);
        }
    }
}

template<typename T>
inline void fine_node<T>::remove_from_cell_subtree(
    const slice &s,
    fine_cell<T> *cell,
    nodal_pocession<T> *problem_context,
    bool force_back_removal)
{
    if(!cell->space.intersects(s))
    {
        return;
    }

    cell->subspace.remove_slice_backwards(
        s,
        problem_context->options.scan_policy.disjoint_weight,
        force_back_removal,
        problem_context->options.quality_policy.max_codim);
    for(fine_cell<T> *child : cell->children)
    {
        // An ignited ceiling owns a new context. Its old cell can remain in
        // this historical cell tree, but the problem slice uses the old
        // context's axis coordinates and must not cross that boundary.
        if(child->node->get_context() == problem_context)
        {
            remove_from_cell_subtree(s, child, problem_context);
        }
    }
}

template<typename T>
inline void fine_node<T>::remove_from_node_subtree(
    const slice &s,
    fine_node<T> *node,
    nodal_pocession<T> *problem_context,
    fine_cell<T> *preferred_cell,
    bool force_preferred_removal)
{
    if(node->get_context() != problem_context)
    {
        return;
    }

    if(preferred_cell != nullptr)
    {
        remove_from_cell_subtree(
            s,
            preferred_cell,
            problem_context,
            force_preferred_removal);
    }
    for(fine_cell<T> *cell : node->cells)
    {
        if(cell != preferred_cell)
        {
            remove_from_cell_subtree(s, cell, problem_context);
        }
    }
}

template<typename T>
inline void fine_node<T>::remove_problem(
    const slice &s,
    fine_cell<T> *origin_cell)
{
    nodal_pocession<T> *problem_context = get_context();
    switch(problem_context->options.pruning_policy)
    {
    case fine_tree_pruning_policy::current_hc:
    {
        search_space pieces =
            origin_cell->subspace.back().remove_slice(s);
        origin_cell->subspace.pop_back();
        origin_cell->subspace.concat(std::move(pieces));
        return;
    }

    case fine_tree_pruning_policy::current_cell:
        remove_from_cell(s, origin_cell, true);
        return;

    case fine_tree_pruning_policy::current_node:
        remove_from_node(s, this, origin_cell, true);
        return;

    case fine_tree_pruning_policy::descendant_subtree:
        remove_from_node_subtree(
            s, this, problem_context, origin_cell, true);
        return;

    case fine_tree_pruning_policy::ancestor_nodes:
    {
        // Process only the cells owned by the current node and its ancestors.
        // Do not enter either the current node's descendants or off-path
        // child subtrees of an ancestor.
        remove_from_node(s, this, origin_cell, true);
        if(is_nodal())
        {
            return;
        }

        // Cells grow monotonically along the parent chain. If cutting the
        // problem slice from the current path cell already exceeds the
        // quality limit, the corresponding parent cells cannot improve.
        const index_t max_codim =
            problem_context->options.quality_policy.max_codim;
        if(!origin_cell->space.is_slice_good(s, max_codim))
        {
            return;
        }

        fine_node<T> *ancestor = parent;
        fine_cell<T> *path_cell = origin_cell->parent;
        while(ancestor != nullptr
              && ancestor->get_context() == problem_context
              && path_cell != nullptr)
        {
            if(!path_cell->space.is_slice_good(s, max_codim))
            {
                break;
            }

            remove_from_node(s, ancestor, path_cell);
            if(ancestor->is_nodal())
            {
                break;
            }
            ancestor = ancestor->parent;
            path_cell = path_cell->parent;
        }
        return;
    }

    case fine_tree_pruning_policy::ancestor_fanout:
        break;
    }

    // Work from the local region outwards. First process the current node's
    // complete descendant subtree. At each ancestor, process the ancestor's
    // own cells and every off-path child subtree, skipping the child subtree
    // already visited at the previous step.
    remove_from_node_subtree(
        s, this, problem_context, origin_cell, true);
    if(is_nodal())
    {
        return;
    }

    fine_node<T> *visited_child = this;
    fine_node<T> *ancestor = parent;
    while(ancestor != nullptr
          && ancestor->get_context() == problem_context)
    {
        remove_from_node(s, ancestor);
        for(fine_node<T> *child : ancestor->children)
        {
            if(child != visited_child)
            {
                remove_from_node_subtree(
                    s, child, problem_context);
            }
        }

        if(ancestor->is_nodal())
        {
            break;
        }
        visited_child = ancestor;
        ancestor = ancestor->parent;
    }
}

template<typename T>
inline fine_node<T> *fine_node<T>::isolate(point p, fine_cell<T> *target_cell, HC *target_hc)
{
    assert(target_cell->space.contains(p));
    assert(target_hc->contains(p));
    fine_node<T> *current_node = this;
    fine_cell<T> *current_cell = target_cell;
    index_t next_n = current_node->is_nodal() ? 0 : current_node->n + 1;
    HC *current_hc = target_hc;
    while(next_n < get_context()->hc_info.dimension)
    {
        index_t next_i = p[next_n];
        fine_node<T> *next_node = current_node->add_child(next_n, next_i, current_node->get_info());
        const auto &[with_i, without_i] = current_hc->split(next_n, next_i);
        fine_cell<T> *next_cell = next_node->add_cell(fine_cell<T>{
            .parent = current_cell,
            .node = next_node,
            .space = with_i,
            .children = {},
            .subspace = search_space{with_i}
        });
        *current_hc = without_i;
        current_cell->subspace.prune_empty(); /* optional */
        // prepare for next iteration
        current_node = next_node;
        current_cell = next_cell;
        current_hc = &current_cell->subspace.back();
        next_n++;
    }
    return current_node;
}

template<typename T>
inline fine_node<T> *fine_node<T>::normalize(point p, fine_cell<T> *target_cell, fine_node<T> *final_node)
{
    std::vector<fine_node<T>*> nodes(get_context()->hc_info.dimension+1, nullptr);
    fine_node<T> *current_node = final_node;
    while(current_node != this)
    {
        nodes[current_node->n+1] = current_node;
        current_node = current_node->parent;
    }
    index_t next_n = current_node->is_nodal() ? 0 : current_node->n + 1;
    nodes[next_n] = this;
    
    auto try_isolate = [current_node=this, &p, target_cell, &nodes](HC *target_hc)
    -> void {
        fine_cell<T> *current_cell = target_cell;
        HC *current_hc = target_hc;
        index_t next_n = current_node->is_nodal() ? 0 : current_node->n + 1;
        auto context = current_node->get_context();
        while(next_n < context->hc_info.dimension)
        {
            index_t next_i = p[next_n];
            if(!(*current_hc)[next_n].contains(p[next_n]))
            {
                // next axis does not contain the coordinate of p: done
                return;
            }
            assert(nodes[next_n] != nullptr);
            const auto &[with_i, without_i] = current_hc->split(next_n, next_i);
            fine_cell<T> *next_cell = nodes[next_n]->add_cell(fine_cell<T>{
                .parent = current_cell,
                .node = nodes[next_n],
                .space = with_i,
                .children = {},
                .subspace = search_space{with_i}
            });
            *current_hc = without_i;
            current_cell = next_cell;
            current_hc = &current_cell->subspace.back();
            next_n++;
        }
    };

    for(HC &hc : target_cell->subspace)
    {
        try_isolate(&hc);
    }
    index_t n1 = next_n + 1;
    return nodes[n1];
}

template<typename T>
inline void fine_node<T>::ignite()
{
    fine_tree_options options = context->options;
    state s = context->hc_info.s;
    moveseq mvs = to_action();
    for(full_move mv : mvs)
    {
        s.apply_move<true>(mv);
    }
    s.submit<true>();
    auto [hc_info, ss] = HC_info::build_HC(s);
    HC universe = hc_info.universe;
    pocessed_context = std::make_unique<nodal_pocession<T>>(nodal_pocession<T>{
        .hc_info = std::move(hc_info),
        .node_pool = {},
        .cell_pool = {
            fine_cell<T>{
                .parent = nullptr,
                .node = this,
                .space = std::move(universe),
                .children = {},
                .subspace = std::move(ss)
            }
        },
        .options = options,
        .verified_terminal = false
    });
    // clear the old cells which are related to the old context
    cells.clear(); 
    cells.push_back(&pocessed_context->cell_pool.back());
}

template<typename T>
inline moveseq fine_node<T>::to_action()
{
    assert(is_ceiling());
    int dim = context->hc_info.dimension;
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
        oss << ", cells#=" << node.cells.size() << "}\n";

        for(size_t cell_index = 0; cell_index < node.cells.size(); ++cell_index)
        {
            const auto &cell = node.cells[cell_index];
            oss << indent << "  cell[" << cell_index << "]\n";
            oss << indent << "    space:\n";
            oss << cell->space.to_string(false);
            oss << indent << "    subspace:\n";
            oss << cell->subspace.to_string();
        }
        
        oss << indent << "  children#=" << node.children.size() << "\n";
        for(size_t child_index = 0; child_index < node.children.size(); ++child_index)
        {
            oss << indent << "    child[" << child_index << "]\n" << std::flush;
            self(*node.children[child_index], depth + 4, self);
        }
    };

    dump_node(*this, 0, dump_node);
    return oss.str();
}
