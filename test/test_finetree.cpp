#undef NDEBUG
#include <tuple>
#include <ranges>
#include <cassert>
#include <type_traits>
#include <utility>
#include <set>
#include "state.h"
#include "pgnparser.h"
#include "finetree.h"
#include "utils.h"

using test_node = fine_node<>;
static_assert(std::is_same_v<decltype(finetree_nearby_ceiling(std::declval<test_node *>())), test_node *>);
static_assert(std::is_same_v<decltype(std::declval<test_node &>().get_child(0)), test_node *>);
static_assert(std::is_same_v<decltype(std::declval<const test_node &>().get_child(0)), const test_node *>);
static_assert(std::is_same_v<decltype(std::declval<const test_node &>().get_parent()), const test_node *>);
static_assert(std::is_same_v<decltype(finetree_nearby_ceiling(std::declval<const test_node *>())), const test_node *>);
static_assert(std::is_same_v<decltype(std::declval<const test_node &>().get_hc_info()), const HC_info &>);
static_assert(std::is_same_v<decltype(std::declval<const test_node &>().get_children()[0]), const test_node *const &>);

template<class Node>
concept exposes_context = requires(Node &node) { node.get_context(); };
static_assert(!exposes_context<test_node>);
static_assert(!exposes_context<const test_node>);

template<class Node>
concept exposes_context_type = requires { typename Node::nodal_pocession; };
static_assert(!exposes_context_type<test_node>);

template<class Node>
concept can_ignite = requires(Node &node) { node.ignite(); };
static_assert(can_ignite<test_node>);
static_assert(!can_ignite<const test_node>);

std::string str = R"(
[Mode "5D"]
[Board "Very Small - Open"]

1. Bb2+ / Nxb2 
2. N>>xd3 / (1T1)Bc3+ 
3. Bb2 

)";

fine_node<> *goto_next_nodal(fine_node<> *node)
{
    auto children = node->get_children();
    while(children.size()>0)
    {
        node = children[0];
        children = node->get_children();
    }
    assert(node->is_ceiling());
    return node;
}

template<HCOrdering Order>
void collect_tree_actions(test_node *node, const Order &order,
                          std::set<moveseq> &actions)
{
    // Expansion may materialize whole branches at once. Every yielded
    // coordinate must still identify exactly one child of this prefix.
    std::set<index_t> coordinates;
    for(index_t coordinate : node->search(order))
    {
        assert(coordinates.insert(coordinate).second);
        assert(node->get_child(coordinate));
    }
    coordinates.clear();
    for(auto *child : node->get_children())
    {
        assert(coordinates.insert(child->get_i()).second);
        assert(node->get_child(child->get_i()) == child);
        if(child->is_ceiling())
        {
            auto moves = child->to_action();
            assert(node->get_hc_info().get_state().can_apply(
                action::from_moveseq(moves, node->get_hc_info().get_state())));
            assert(actions.insert(std::move(moves)).second);
        }
        else
        {
            collect_tree_actions(child, order, actions);
        }
    }
}

void test_normalization_preserves_actions(const state &position)
{
    auto [info, space] = HC_info::build_HC(position);
    std::set<moveseq> expected;
    for(auto moves : info.search<natural_HC_ordering, hc_policy::balanced>(std::move(space)))
        expected.insert(std::move(moves));
    assert(!expected.empty());

    auto check = [&](const auto &order) {
        auto root = test_node::make_root(position);
        std::set<moveseq> actual;
        collect_tree_actions(root.get(), order, actual);
        // Unique prefixes must preserve every continuation, not just remove
        // duplicate coordinates by discarding their search regions.
        assert(actual == expected);
    };
    check(natural_HC_ordering{});
    for(unsigned seed : {7u, 42u})
    {
        std::mt19937 rng(seed);
        check(random_HC_ordering(info.get_universe(), rng));
    }
}

int main()
{
    state s = state::from_pgn<true>(str);
    test_normalization_preserves_actions(s);

    // A terminal check retains a witness branch and reuses it on later checks.
    auto checked_root = fine_node<>::make_root(s);
    assert(checked_root->get_children().empty());
    assert(!checked_root->is_terminal());
    const auto child_count = checked_root->get_children().size();
    assert(child_count > 0);
    auto *ceiling = finetree_nearby_ceiling(checked_root.get());
    assert(ceiling && s.can_apply(action::from_moveseq(ceiling->to_action(), s)));
    assert(!checked_root->is_terminal(natural_HC_ordering{}));
    assert(checked_root->get_children().size() == child_count);
    const auto &const_root = *checked_root;
    assert(finetree_nearby_ceiling(&const_root) == ceiling);

    std::cout << s.to_string() << std::endl;
    auto root = fine_node<>::make_root(s);
    fine_node<> *node = root.get();

    // First branch: consume one expansion via search, then navigate to its child
    std::cout << "First branch of root:\n";
    for(index_t i : node->search(natural_HC_ordering{}))
    {
        std::cout << i << ' ';
        node = node->get_child(i);
        break; // only first expansion
    }
    std::cout << "\n\n";
    std::cout << (node->to_string()) << std::endl;

    // Remaining expansions on root
    std::cout << "Other children of root:\n";
    for(index_t i : root->search(natural_HC_ordering{}))
    {
        std::cout << i << ' ';
    }
    std::cout << "\n\n";

    // Search within the first child for more expansions
    std::cout << "Other children of first node:\n";
    for(index_t i : node->search(natural_HC_ordering{}))
    {
        std::cout << i << ' ';
    }
    std::cout << "\n\n";

    std::cout << "Search completed." << std::endl;

    // Navigate to celling, get action, ignite, and repeat
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    node->ignite();

    node->search(natural_HC_ordering{}).first();  // expand the ignited node
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    node->ignite();

    node->search(natural_HC_ordering{}).first();
    // Navigate to celling by following the first child chain
    {
        auto children = node->get_children();
        while(!children.empty())
        {
            node = children[0];
            children = node->get_children();
        }
    }
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    return 0;
}
