#ifndef FINETREE_H
#define FINETREE_H

#include <memory>
#include <deque>
#include <optional>
#include "state.h"
#include "geometry.h"
#include "hypercuboid.h"
#include "integer_set.h"
#include "generator.h"

class fine_node;

struct fine_cell
{
    fine_cell *parent;
    fine_node *node; // the associated node for this cell
    HC space;
    std::vector<fine_cell*> children;
    search_space subspace;
};

struct nodal_pocession;

class fine_node
{
    fine_node* parent;
    
    // only for nodal nodes
    std::unique_ptr<nodal_pocession> pocessed_context;
    // non-owning pointer to the pocessed_context of its ancestor
    nodal_pocession *context;
    index_t n, i; // only meaningful for temporary nodes
    std::vector<fine_cell*> cells;
public:
    // copying needs to explicitly set the pareent pointer of its children
    // so they are deleted because they are not used for now
    fine_node(const fine_node&) = delete;
    fine_node& operator=(const fine_node&) = delete;
    fine_node(fine_node&& other) = delete;
    fine_node& operator=(fine_node&& other) = delete;
    
    fine_node(fine_node *parent, state s);
    fine_node(fine_node *parent, index_t n, index_t i);
    static std::unique_ptr<fine_node> make_nodal(fine_node *parent, state s);
    static std::unique_ptr<fine_node> make_temproary(fine_node *parent, index_t n, index_t i);

    bool is_nodal() const { return pocessed_context != nullptr; }

    generator<index_t> search();
    fine_node *expand();
    std::optional<std::tuple<point, fine_cell*, HC*>> explore();
    void remove_slice(const slice&);

    fine_node *isolate(point, fine_cell*, HC*);
    void normalize(point, fine_cell*, fine_node*);

    std::string to_string() const;
};

struct nodal_pocession
{
    HC_info info;
    std::deque<fine_node> node_pool;
    std::deque<fine_cell> cell_pool;
};

#endif /* FINETREE_H */
