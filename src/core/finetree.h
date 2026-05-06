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
    struct fine_cell
    {
        fine_cell *parent;
        fine_node *node; // the node containing this cell
        HC space;
        std::vector<fine_cell*> children;
        search_space subspace;
        
        fine_cell(fine_cell *p, fine_node *n, HC s, search_space ss)
        : parent{p}, node{n}, space{std::move(s)}, children{}, subspace{std::move(ss)} {}
        
        fine_cell(const fine_cell&) = delete;
        fine_cell& operator=(const fine_cell&) = delete;
        
        fine_cell(fine_cell&& other) noexcept;
        fine_cell& operator=(fine_cell&& other) noexcept;
    };
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
    std::vector<fine_cell> cells;

public:
    // copying needs to explicitly set the pareent pointer of its children
    // so it is deleted because it is not used for now
    fine_node(const fine_node&) = delete;
    fine_node& operator=(const fine_node&) = delete;
    
    fine_node(fine_node *parent, state s);
    fine_node(fine_node *parent, index_t n, index_t i);
    fine_node(fine_node&& other) noexcept;
    fine_node& operator=(fine_node&& other) noexcept;
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

#endif /* FINETREE_H */
