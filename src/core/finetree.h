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

/*
 There are a few types of fine_nodes:
 + nodal: Represents the initial game state or a game state where an action is
 just performed and submitted. Thus, the `pocessed_context` is nonempty and contains
 the info for further expansion.
 + temporary: Represents the pseudo-state where some semimoves are queued to be applied.
 The next semimove is recorded by `n` and `i`. Info for expansion is stored in `context`.
 + terminal: The temporary fine_node that contains the last semimove: after this, a
 valid action can be applied. A terminal node can also become the nodal fine_node via
 calling the `ignite` method. In this case, `context` still stores the old info
 while `pocessed_context` store the new info.
 + root: A node which is nodal but not terminal. Its `parent` pointer shall be null.
 */

class fine_node
{
    fine_node* parent;
    // only for nodal nodes
    std::unique_ptr<nodal_pocession> pocessed_context;
    // non-owning pointer to the pocessed_context of its ancestor
    nodal_pocession *context;
    index_t n, i; // only meaningful for temporary nodes
    std::vector<fine_cell*> cells;
    std::vector<fine_node*> children;
public:
    // copying needs to explicitly set the pareent pointer of its children
    // so they are deleted because they are not used for now
    fine_node(const fine_node&) = delete;
    fine_node& operator=(const fine_node&) = delete;
    fine_node(fine_node&& other) = delete;
    fine_node& operator=(fine_node&& other) = delete;
    
    fine_node(fine_node *parent, state s);
    fine_node(fine_node *parent, index_t n, index_t i);
    static std::unique_ptr<fine_node> make_root(fine_node *parent, state s);
    static std::unique_ptr<fine_node> make_temproary(fine_node *parent, index_t n, index_t i);

    fine_cell *add_cell(fine_cell &&cell);
    fine_node *add_child(index_t n, index_t i);

    bool is_nodal() const { return pocessed_context != nullptr; }
    bool is_terminal() const;

    fine_node *get_child(index_t i) const;
    fine_node *get_parent() const { return parent; }
    const std::vector<fine_node*> get_children() const { return children; };
    nodal_pocession *get_context() const; /* returns the newer context */

    // expansion methods
    generator<index_t> search();
    fine_node *expand();
    std::optional<std::tuple<point, fine_cell*, HC*>> explore();
    void remove_slice(const slice&);

    fine_node *isolate(point, fine_cell*, HC*);
    fine_node *normalize(point, fine_cell*, fine_node*);

    void ignite(); /* make a terminal node also a nodal node */

    moveseq to_action(); /* only avialible for terminal nodes */

    std::string to_string() const;
};

struct nodal_pocession
{
    HC_info info;
    std::deque<fine_node> node_pool;
    std::deque<fine_cell> cell_pool;
};

#endif /* FINETREE_H */
