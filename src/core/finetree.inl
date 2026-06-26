#include <cassert>
#include <sstream>
#include <map>

#include "finetree.h"

template<typename T>
fine_node<T>::fine_node(fine_node *parent, state s, T info_value)
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
                .subspace = std::move(ss)
            }
        }
    });
    cells.push_back(&pocessed_context->cell_pool.back());
}

template<typename T>
fine_node<T>::fine_node(fine_node *parent, index_t n, index_t i, T info_value)
: parent{parent}, pocessed_context{nullptr}, context{parent->get_context()}, n{n}, i{i}, cells{}, info{std::move(info_value)} {}

template<typename T>
std::unique_ptr<fine_node<T>> fine_node<T>::make_root(state s, T info)
{
    return std::unique_ptr<fine_node<T>>(new fine_node(nullptr, s, std::move(info)));
}

template<typename T>
std::unique_ptr<fine_node<T>> fine_node<T>::make_temproary(fine_node *parent, index_t n, index_t i, T info)
{
    return std::unique_ptr<fine_node<T>>(new fine_node(parent, n, i, std::move(info)));
}

template<typename T>
fine_cell<T> *fine_node<T>::add_cell(fine_cell<T> &&cell)
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
fine_node<T> *fine_node<T>::add_child(index_t n, index_t i, T info)
{
    auto ctx = get_context();
    ctx->node_pool.emplace_back(this, n, i, std::move(info));
    fine_node<T> *child = &ctx->node_pool.back();
    children.push_back(child);
    return child;
}

template<typename T>
bool fine_node<T>::is_ceiling() const
{
    // is_ceiling needs the older context, not the newer one
    return n + 1 == context->hc_info.dimension;
}

template<typename T>
fine_node<T> *fine_node<T>::get_nearby_ceiling()
{
    fine_node<T> *current = this;
    while (true) {
        if (!current->is_nodal() && current->is_ceiling()) {
            return current;
        }
        if (current->children.empty()) {
            return nullptr;
        }
        current = current->children[0];
    }
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
fine_node<T> *fine_node<T>::get_child(index_t i) const
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
nodal_pocession<T> *fine_node<T>::get_context() const
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

template<typename T>
generator<index_t> fine_node<T>::search()
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
fine_node<T> *fine_node<T>::expand()
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
std::optional<std::tuple<point, fine_cell<T> *, HC *>> fine_node<T>::explore()
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
                    remove_slice(*problem);
                }
                else
                {
                    // otherwise we are done
                    return std::optional<std::tuple<point, fine_cell<T>*, HC*>>{std::in_place, *pt_opt, cell, &hc};
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
void fine_node<T>::remove_slice(const slice &s)
{
    //current policy: remove slices just for cells in this node
    for(fine_cell<T> *cell : cells)
    {
        search_space adjoined;
        for(const HC &hc : cell->subspace)
        {
            search_space new_ss = hc.remove_slice_carefully(s);
            adjoined.concat(std::move(new_ss));
        }
        cell->subspace = std::move(adjoined);
    }
}

template<typename T>
fine_node<T> *fine_node<T>::isolate(point p, fine_cell<T> *target_cell, HC *target_hc)
{
    // dprint("ISOLATE: n =", n);
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
fine_node<T> *fine_node<T>::normalize(point p, fine_cell<T> *target_cell, fine_node<T> *final_node)
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
void fine_node<T>::ignite()
{
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
                .subspace = std::move(ss)
            }
        },
        .verified_terminal = false
    });
    cells.push_back(&pocessed_context->cell_pool.back());
}

template<typename T>
moveseq fine_node<T>::to_action()
{
    assert(is_ceiling());
    std::vector<index_t> pt(context->hc_info.dimension);
    auto current = this;
    while(!current->is_nodal())
    {
        pt[current->n] = current->i;
        current = current->parent;
    }
    return context->hc_info.to_action(pt);
}

template<typename T>
std::string fine_node<T>::to_string() const
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