#ifndef FINETREE_H
#define FINETREE_H

#include <concepts>
#include <memory>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>
#include "state.h"
#include "geometry.h"
#include "hypercuboid.h"
#include "integer_set.h"
#include "generator.h"
#include "ordering.h"

template<typename T = std::monostate>
    requires std::default_initializable<T>
class fine_node;

enum class fine_builder { regular, solid };

struct fine_search_options
{
    fine_builder builder = fine_builder::regular;
    hc_cell_options cells;
    bool reuse_local_classes = true;
    std::size_t local_class_capacity = 8; // Additional validated classes retained beside the current class.
};

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
    1. The function `finetree_nearby_ceiling(node)` follows the first-child chain to a ceiling node. If the current node is ceiling, it returns itself.
    2. `is_terminal` checks if the node is terminal. It may add children to the node if it is not terminal but has no children yet. Note: if the node is ceiling but not ignited, `is_terminal` will always return false.

Associated information:
    The optional template parameter T allows adding additional information 
    to each node in stack. T needs to be default-constructible: expand(),
    search(), etc. adds unspecific amount of new nodes to the tree.
 */

template<typename T>
    requires std::default_initializable<T>
class fine_node
{
    struct nodal_pocession;

    fine_node<T> *parent;
    // only for nodal nodes
    std::unique_ptr<nodal_pocession> pocessed_context;
    // non-owning pointer to the pocessed_context of its ancestor
    nodal_pocession *context;
    index_t n, i; // only meaningful for temporary nodes
    // The full restriction is the parent prefix, this next-axis mask, and
    // the context universe on later axes. Avoid a full HC copy in every node.
    integer_set remaining;
    struct completion_record
    {
        point coordinates;
        std::shared_ptr<const HC_info::completion_query> base;
        completion_record(point p, std::shared_ptr<const HC_info::completion_query> inherited)
            : coordinates(std::move(p)), base(std::move(inherited)) {}
    };
    struct search_continuation
    {
        std::shared_ptr<HC_info::completion_query> global;
        std::unique_ptr<HC_info::local_cursor> local;
    };
    std::shared_ptr<const completion_record> completion;
    std::unique_ptr<search_continuation> pending;
    // Most prefix nodes have exactly one child. Keep that pointer inline and
    // allocate a vector only when a second coordinate is admitted.
    class child_list
    {
        fine_node *single = nullptr;
        std::unique_ptr<std::vector<fine_node *>> multiple;
    public:
        std::size_t size() const noexcept { return multiple ? multiple->size() : single ? 1 : 0; }
        fine_node *const *data() const noexcept { return multiple ? multiple->data() : &single; }
        fine_node *const *begin() const noexcept { return data(); }
        fine_node *const *end() const noexcept { return data() + size(); }
        fine_node *operator[](std::size_t i) const noexcept { return data()[i]; }
        void push_back(fine_node *child)
        {
            if(multiple) multiple->push_back(child);
            else if(!single) single = child;
            else multiple = std::make_unique<std::vector<fine_node *>>(std::initializer_list<fine_node *>{single, child});
        }
    } children;
    T info;

    /* Return the newer context. */
    nodal_pocession *get_context() & noexcept;
    HC_info::completion_query *query() const noexcept { return pending ? pending->global.get() : nullptr; }
    search_continuation &pending_search()
    {
        if(!pending) pending = std::make_unique<search_continuation>();
        return *pending;
    }

    // -- internal tree-building helpers -- //
    void ignite_impl(const state &s, fine_search_options options);
    fine_node<T> *add_child(index_t n, index_t i);
    template<HCOrdering Order>
    fine_node<T> *expand_one(const Order &order);

public:
    // copying/moving a fine_node needs to explicitly set the parent pointer of its children
    // so they are deleted because they are not used for now
    fine_node(const fine_node &) = delete;
    fine_node &operator=(const fine_node &) = delete;
    fine_node(fine_node &&other) = delete;
    fine_node &operator=(fine_node &&other) = delete;

    // -- constructors (prefer not to use directly) -- //
    fine_node(fine_node *parent, const state &s, T info = T{}, fine_search_options options = {});
    fine_node(fine_node *parent, index_t n, index_t i);
    fine_node(fine_node *parent, index_t n, index_t i, T info);
    static std::unique_ptr<fine_node<T>> make_temproary(fine_node *parent, index_t n, index_t i, T info = T{});

    // -- factory -- //
    static std::unique_ptr<fine_node<T>> make_root(
        const state &s, T info = T{}, fine_search_options options = {});

    // -- queries -- //
    bool is_nodal() const noexcept { return pocessed_context != nullptr; }
    bool is_ceiling() const noexcept;
    T &get_info() & noexcept { return info; }
    const T &get_info() const & noexcept { return info; }
    const T &get_info() const && = delete;
    void set_info(const T &new_info) & { info = new_info; }
    void set_info(T &&new_info) & { info = std::move(new_info); }
    bool get_player() const noexcept;
    // Sibling coordinates are unique. Their continuations query the same
    // shared cell forest under independent prefix restrictions.
    fine_node<T> *get_child(index_t i) & noexcept;
    const fine_node<T> *get_child(index_t i) const & noexcept;
    const fine_node<T> *get_child(index_t i) const && = delete;
    fine_node<T> *get_parent() & noexcept { return parent; }
    const fine_node<T> *get_parent() const & noexcept { return parent; }
    const fine_node<T> *get_parent() const && = delete;
    // Borrowed pointer slots; expansion may invalidate the view.
    std::span<fine_node<T> *const> get_children() & noexcept { return {children.data(), children.size()}; }
    std::span<const fine_node<T> *const> get_children() const & noexcept { return {children.data(), children.size()}; }
    std::span<const fine_node<T> *const> get_children() const && = delete;
    const HC_info &get_hc_info() const & noexcept;
    const HC_info &get_hc_info() const && = delete;
    const fine_search_options &get_search_options() const noexcept;
    struct statistics
    {
        hc_cell_tree::statistics cells;
        std::size_t nodes, matching_calls, witness_calls, local_hits, forest_searches;
        std::size_t local_resumes, local_resume_hits;
    };
    statistics search_statistics() const;
    std::string print_semimove() const;
    bool is_terminal() &;
    template<HCOrdering Order>
    bool is_terminal(const Order &order) &; /* non-const because it may add children found during the check */
    index_t get_n() const noexcept { return n; }
    index_t get_i() const noexcept { return i; }

    // -- expansion -- //
    // Admit one coordinate and retain a complete ordered witness branch.
    template<HCOrdering Order>
    fine_node<T> *expand_next(const Order &order) &;
    template<HCOrdering Order>
    generator<index_t> search(Order order) &;
    void ignite() &; /* make a ceiling node also a nodal node */
    moveseq to_action() const; /* only avialible for ceiling nodes */

    // -- debug -- //
    std::string to_string() const;
};

template<typename T>
    requires std::default_initializable<T>
struct fine_node<T>::nodal_pocession
{
    HC_info hc_info;
    HC_info::completion_space search;
    fine_search_options options;
    HC restriction{};
    std::deque<fine_node<T>> node_pool;
    bool verified_terminal; /* value is
    + true if no further expansion is possible
    + false if not terminal or not yet verified
    */
    nodal_pocession(HC_info info, search_space initial, fine_search_options configuration)
        : hc_info(std::move(info)), search(std::move(initial), configuration.cells),
          options(configuration), verified_terminal(false)
    {
        search.reuse_local = configuration.reuse_local_classes;
        search.local_capacity = configuration.local_class_capacity;
    }
};

// Follow the first-child chain to a ceiling node, preserving node constness.
template<class Node>
Node *finetree_nearby_ceiling(Node *node)
{
    while(node && !node->is_ceiling())
    {
        auto children = node->get_children();
        if(children.empty())
        {
            return nullptr;
        }
        node = children.front();
    }
    return node;
}

#include "finetree.inl"

#endif /* FINETREE_H */
