#include "finetree.h"
#include <cassert>
#include <sstream>
#include <map>

//#define DEBUGMSG
#include "debug.h"

fine_node::fine_node(fine_node *parent, state s)
: parent{parent}, pocessed_context{nullptr}, context{nullptr}, n{index_t(-1)}, i{index_t(-1)}, cells{}
{
    auto [info, ss] = HC_info::build_HC(s);
    HC universe = info.universe;
    pocessed_context = std::make_unique<nodal_pocession>(nodal_pocession{
        .info = std::move(info),
        .node_pool = {},
        .cell_pool = {
            fine_cell{
                .parent = nullptr,
                .node = this,
                .space = std::move(universe),
                .subspace = std::move(ss)
            }
        }
    });
    context = pocessed_context.get();
    cells.push_back(&context->cell_pool.back());
}

fine_node::fine_node(fine_node *parent, index_t n, index_t i)
: parent{parent}, pocessed_context{nullptr}, context{parent->context}, n{n}, i{i}, cells{} {}

std::unique_ptr<fine_node> fine_node::make_nodal(fine_node *parent, state s)
{
    return std::unique_ptr<fine_node>(new fine_node(parent, s));
}

std::unique_ptr<fine_node> fine_node::make_temproary(fine_node *parent, index_t n, index_t i)
{
    return std::unique_ptr<fine_node>(new fine_node(parent, n, i));
}

fine_cell *fine_node::add_cell(fine_cell &&cell)
{
    context->cell_pool.push_back(std::move(cell));
    fine_cell *new_cell = &context->cell_pool.back();
    cells.push_back(new_cell);
    new_cell->parent->children.push_back(new_cell);
    return new_cell;
}

fine_node *fine_node::add_child(index_t n, index_t i)
{
    context->node_pool.emplace_back(this, n, i);
    fine_node *child = &context->node_pool.back();
    children.push_back(child);
    return child;
}

generator<index_t> fine_node::search()
{
    fine_node *next_node = expand();
    while(next_node)
    {
        co_yield next_node->i;
        next_node = expand();
    }
}

fine_node *fine_node::expand()
{
    auto ans = explore();
    if(!ans)
    {
        return nullptr;
    }
    auto [pt, cell, hc] = *ans;
    fine_node *final_node = isolate(pt, cell, hc);
    fine_node *next_node = normalize(pt, cell, final_node);
    return next_node;
}

std::optional<std::tuple<point, fine_cell *, HC *>> fine_node::explore()
{
    HC_info &info = context->info;
    for(fine_cell *cell : cells)
    {
        // search for each cell in cells
        while(!cell->subspace.empty())
        {
            // while the search space of this cell is not exhausted
            HC &hc = cell->subspace.back();
            //cell.subspace.hcs.pop_back();
            // try to take a point in this hc
            auto pt_opt = info.take_point(hc);
            if(pt_opt)
            {
                auto problem = info.find_problem(*pt_opt, hc);
                if(problem)
                {
                    // if there is a problem with this point
                    // remove the problem for all relevant cells
                    remove_slice(*problem);
                }
                else
                {
                    // otherwise we are done
                    return std::optional<std::tuple<point, fine_cell*, HC*>>{std::in_place, *pt_opt, cell, &hc};
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

void fine_node::remove_slice(const slice &s)
{
    //current policy: remove slices just for cells in this node
    for(fine_cell *cell : cells)
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

fine_node *fine_node::isolate(point p, fine_cell *target_cell, HC *target_hc)
{
    dprint("ISOLATE: n =", n);
    assert(target_cell->space.contains(p));
    assert(target_hc->contains(p));
    fine_node *current_node = this;
    fine_cell *current_cell = target_cell;
    index_t next_n = current_node->is_nodal() ? 0 : current_node->n + 1;
    HC *current_hc = target_hc;
    while(next_n < context->info.dimension)
    {
        index_t next_i = p[next_n];
        fine_node *next_node = current_node->add_child(next_n, next_i);
        const auto &[with_i, without_i] = current_hc->split(next_n, next_i);
        fine_cell *next_cell = next_node->add_cell(fine_cell{
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

fine_node *fine_node::normalize(point p, fine_cell *target_cell, fine_node *final_node)
{
    std::vector<fine_node*> nodes(context->info.dimension+1, nullptr);
    fine_node *current_node = final_node;
    while(current_node != this)
    {
        nodes[current_node->n+1] = current_node;
        current_node = current_node->parent;
    }
    index_t next_n = current_node->n + 1;
    nodes[next_n] = this;
    
    auto try_isolate = [current_node=this, &p, target_cell, &nodes](HC *target_hc)
    -> void {
        fine_cell *current_cell = target_cell;
        HC *current_hc = target_hc;
        index_t next_n = current_node->is_nodal() ? 0 : current_node->n + 1;
        auto context = current_node->context;
        while(next_n < context->info.dimension)
        {
            index_t next_i = p[next_n];
            if(!(*current_hc)[next_n].contains(p[next_n]))
            {
                // next axis does not contain the coordinate of p: done
                return;
            }
            assert(nodes[next_n] != nullptr);
            const auto &[with_i, without_i] = current_hc->split(next_n, next_i);
            fine_cell *next_cell = nodes[next_n]->add_cell(fine_cell{
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
    index_t n1 = n + 2;
    return nodes[n1];
}

std::string fine_node::to_string() const
{
    std::ostringstream oss;

    const auto dump_node = [&](const fine_node &node, size_t depth, const auto &self) -> void {
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
