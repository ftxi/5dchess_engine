#ifndef FINETREE_H
#define FINETREE_H

#include <memory>
#include <deque>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>
#include "state.h"
#include "geometry.h"
#include "hypercuboid.h"
#include "integer_set.h"
#include "generator.h"

enum class fine_tree_pruning_policy
{
    current_hc,
    current_cell,
    current_node,
    descendant_subtree,
    ancestor_nodes,
    ancestor_fanout
};

struct dynamic_scan_policy
{
    // Equivalent to HC_info::search(): continue while
    // disjoint_weight * disjoint_count < intersect_count.
    // A weight of zero disables early stopping.
    size_t disjoint_weight = 10;
};

struct slice_quality_policy
{
    // Maximum number of partially intersected axes accepted when removing a
    // problem slice from a secondary HC. The critical HC is always removed.
    index_t max_codim = 1;
};

struct fine_tree_options
{
    fine_tree_pruning_policy pruning_policy =
        fine_tree_pruning_policy::current_node;
    dynamic_scan_policy scan_policy;
    slice_quality_policy quality_policy;
};

template<typename T = std::monostate>
class fine_node;

template<typename T = std::monostate>
struct fine_cell
{
    fine_cell *parent;
    fine_node<T> *node; // the associated node for this cell
    HC space;
    std::vector<fine_cell*> children;
    search_space subspace;
};

template<typename T = std::monostate>
struct nodal_pocession;

/*
 There are a few types of fine_nodes:
 + nodal: Represents the initial game state or a game state where an action is
 just performed and submitted. Thus, the `pocessed_context` is nonempty and contains the info for further expansion.
 + temporary: Represents the pseudo-state where some semimoves are queued to be applied. The next semimove is recorded by `n` and `i`. Info for expansion is stored in `context`.
 + ceiling: The temporary fine_node that contains the last semimove: after this, a valid action can be applied. A ceiling node can also become the nodal fine_node via
 calling the `ignite` method. In this case, `context` still stores the old info while `pocessed_context` store the new info.
 + root: A node which is nodal but not ceiling. Its `parent` pointer shall be null.
 + terminal: a nodal node which cannot be expanded further. 

 Usage:
    1. Create a root node with `make_root` method.
    2. Call `search` method to get the indices of the next semimoves to be applied. It returns a index.
    3. Use get_child(i) to get the child node with the index. This child node is a temporary node.
    4. The info for each node is set by the default constructor when a new node is created. You can use `get_info` and `set_info` to read and modify the info.
    5. Each time one search is performed, a full branch is created. To access the remaining nodes, you can use `get_children` to get the list of children. Each child is a temporary node (whose index you might not know in advance). Use `get_children` `get_child(i)` and `get_parent` to navigate. Use `is_ceiling` to check if a node is ceiling.
    6. When you reach a ceiling node, you can call `to_action` to get the moveseq represented by the branch culminated by this node, and thus apply it elsewhere.
    7. If further expansion is needed, you can call `ignite` to make the ceiling node a nodal node. Further expansion can be performed by starting over from step 2. PS: You can use `is_nodal` to check if a node is ignited.

Additional tools:
    1. `get_nearby_ceiling` returns the nearest ceiling node in the ancestor chain. If the current node is ceiling, it returns itself.
    2. `is_terminal` checks if the node is terminal. It may add children to the node if it is not terminal but has no children yet. Note: if the node is ceiling but not ignited, `is_terminal` will always return false.
 */

template<typename T>
class fine_node
{
    fine_node<T> *parent;
    // only for nodal nodes
    std::unique_ptr<nodal_pocession<T>> pocessed_context;
    // non-owning pointer to the pocessed_context of its ancestor
    nodal_pocession<T> *context;
    index_t n, i; // only meaningful for temporary nodes
    std::vector<fine_cell<T>*> cells;
    std::vector<fine_node<T>*> children;
    T info;

    // -- internal tree-building helpers -- //
    fine_cell<T> *add_cell(fine_cell<T> &&cell);
    fine_node<T> *add_child(index_t n, index_t i, T info = T{});
    fine_node<T> *expand();
    std::optional<std::tuple<point, fine_cell<T>*, HC*>> explore();
    void remove_problem(const slice&, fine_cell<T>* origin_cell);
    void remove_from_cell(const slice&, fine_cell<T>*, bool force_back_removal = false);
    void remove_from_node(const slice&, fine_node<T>*, fine_cell<T>* preferred_cell = nullptr, bool force_preferred_removal = false);
    void remove_from_cell_subtree(const slice&, fine_cell<T>*, nodal_pocession<T>*, bool force_back_removal = false);
    void remove_from_node_subtree(const slice&, fine_node<T>*, nodal_pocession<T>*, fine_cell<T>* preferred_cell = nullptr, bool force_preferred_removal = false);
    fine_node<T> *isolate(point, fine_cell<T>*, HC*);
    fine_node<T> *normalize(point, fine_cell<T>*, fine_node<T>*);

public:
    // copying/moving a fine_node needs to explicitly set the parent pointer of its children
    // so they are deleted because they are not used for now
    fine_node(const fine_node&) = delete;
    fine_node &operator=(const fine_node&) = delete;
    fine_node(fine_node &&other) = delete;
    fine_node &operator=(fine_node &&other) = delete;

    // -- constructors (prefer not to use directly) -- //
    fine_node(fine_node *parent, state s, T info = T{}, fine_tree_options options = {});
    fine_node(fine_node *parent, index_t n, index_t i, T info = T{});
    static std::unique_ptr<fine_node<T>> make_temproary(fine_node *parent, index_t n, index_t i, T info = T{});

    // -- factory -- //
    static std::unique_ptr<fine_node<T>> make_root(state s, T info = T{}, fine_tree_options options = {});

    // -- queries -- //
    bool is_nodal() const { return pocessed_context != nullptr; }
    bool is_ceiling() const;
    T &get_info() { return info; }
    const T &get_info() const { return info; }
    void set_info(const T &new_info) { info = new_info; }
    void set_info(T &&new_info) { info = std::move(new_info); }
    fine_node<T> *get_child(index_t i) const;
    fine_node<T> *get_parent() const { return parent; }
    const std::vector<fine_node<T>*> get_children() const { return children; };
    nodal_pocession<T> *get_context() const; /* returns the newer context */
    std::string print_semimove() const;
    fine_node<T> *get_nearby_ceiling();
    bool is_terminal(); /* non-const because it may add children found during the check */
    index_t get_n() const { return n; }
    index_t get_i() const { return i; }

    // -- expansion -- //
    generator<index_t> search();
    void ignite(); /* make a ceiling node also a nodal node */
    moveseq to_action(); /* only avialible for ceiling nodes */
    /* gen_all_children: ignite if needed, then search all children
    returns true if the node is not terminal
     */
    bool gen_all_children();
    std::string to_string() const;
};

template<typename T>
struct nodal_pocession
{
    HC_info hc_info;
    std::deque<fine_node<T>> node_pool;
    std::deque<fine_cell<T>> cell_pool;
    fine_tree_options options;
    bool verified_terminal; /* value is
    + true if no further expansion is possible
    + false if not terminal or not yet verified
    */
};

#include "finetree.inl"

#endif /* FINETREE_H */
