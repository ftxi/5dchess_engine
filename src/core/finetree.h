#ifndef FINETREE_H
#define FINETREE_H

#include <memory>
#include <vector>
#include <optional>
#include "state.h"
#include "geometry.h"
#include "hypercuboid.h"
#include "integer_set.h"
#include "generator.h"

class fine_node
{
    fine_node* parent;
    struct nodal_pocession
    {
        HC_info info;
        std::vector<std::unique_ptr<fine_node>> node_pool;
    };
    // only for nodal nodes
    std::unique_ptr<nodal_pocession> pocessed_context;
    // non-owning pointer to the pocessed_context of its ancestor
    nodal_pocession *context;
    index_t n, i; // only meaningful for temporary nodes
    struct fine_cell
    {
        HC space;
        std::vector<fine_node*> children;
        search_space subspace;
    };
    std::vector<fine_cell> cells;

public:
    fine_node(fine_node *parent, state s);
    fine_node(fine_node *parent, index_t n, index_t i);
    static std::unique_ptr<fine_node> make_nodal(fine_node *parent, state s);
    static std::unique_ptr<fine_node> make_temproary(fine_node *parent, HC_info *info, index_t n, index_t i);

    generator<index_t> search();
    fine_node *expand();
    std::optional<point> explore();
    void remove_slice(const slice &);
    fine_node *isolate(point, fine_cell*);
    void normalize();
};

#endif /* FINETREE_H */