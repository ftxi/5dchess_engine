#include "finetree.h"
#include "debug.h"

fine_node::fine_node(fine_node *parent, state s)
: parent{parent}, pocessed_context{nullptr}, context{nullptr}, n{-1}, i{-1}, cells{}
{
    auto [info, ss] = HC_info::build_HC(s);
    pocessed_context = std::make_unique<nodal_pocession>(nodal_pocession{std::move(info), {}});
    context = pocessed_context.get();
    cells.push_back(fine_cell{
        .space = context->info.universe,
        .children = {},
        .subspace = ss
    });
}

fine_node::fine_node(fine_node *parent, index_t n, index_t i)
: parent{parent}, pocessed_context{nullptr}, context{parent->context}, n{n}, i{i}, cells{}
{
}

std::unique_ptr<fine_node> fine_node::make_nodal(fine_node *parent, state s)
{
    return std::make_unique<fine_node>(parent, s);
}

std::unique_ptr<fine_node> fine_node::make_temproary(fine_node *parent, HC_info *info, index_t n, index_t i)
{
    return std::make_unique<fine_node>(parent, n, i);
}

std::optional<point> fine_node::explore()
{
    HC_info &info = context->info;
    for(fine_cell &cell : cells)
    {
        // search for each cell in cells
        while(!cell.subspace.hcs.empty())
        {
            // while the search space of this cell is not exhausted
            HC hc = cell.subspace.hcs.back();
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
                    return pt_opt;
                }
            }
        }
    }
    // explored all cells without finding a solution; report failure
    return std::nullopt;
}

void fine_node::remove_slice(const slice &s)
{
    //current policy: remove slices just for cells in this node
    for(fine_cell &cell : cells)
    {
        search_space adjoined;
        for(const HC &hc : cell.subspace.hcs)
        {
            search_space new_ss = hc.remove_slice_carefully(s);
            adjoined.concat(std::move(new_ss));
        }
        cell.subspace = std::move(adjoined);
    }
}

fine_node* fine_node::isolate(point p, fine_cell *target_cell)
{
    
}