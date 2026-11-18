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

std::vector<point> legal_completions(const HC_info &info, search_space pending)
{
    std::vector<point> result;
    while(!pending.empty())
    {
        auto domain = std::move(pending.back());
        pending.pop_back();
        auto p = info.take_point(domain);
        if(!p) continue;
        if(!info.find_problem(*p, info.get_universe())) result.push_back(*p);
        pending.concat(std::move(domain).remove_point(*p));
    }
    return result;
}

template<HCOrdering Order>
void check_ordered_prefixes(test_node *node, const std::vector<point> &legal,
                            const Order &order)
{
    const auto axis = node->is_nodal() ? 0 : node->get_n() + 1;
    if(axis >= node->get_hc_info().dimension()) return;
    integer_set feasible;
    std::vector<const point *> matching;
    for(const auto &p : legal)
    {
        bool matches = true;
        for(auto *prefix = node; !prefix->is_nodal(); prefix = prefix->get_parent())
            matches &= p[prefix->get_n()] == prefix->get_i();
        if(matches) { feasible.insert(p[axis]); matching.push_back(&p); }
    }
    for([[maybe_unused]] auto coordinate : node->search(order)) {}
    std::vector<index_t> expected;
    for(auto coordinate : order(axis, feasible)) expected.push_back(coordinate);
    assert(node->get_children().size() == expected.size());
    for(std::size_t i = 0; i < expected.size(); ++i)
    {
        auto *child = node->get_children()[i];
        assert(child->get_i() == expected[i]);
        check_ordered_prefixes(child, legal, order);
    }
}

void check_changing_order_prefixes(test_node *node, const std::vector<point> &legal,
                                   const random_HC_ordering &random_order)
{
    if(node->is_ceiling()) return;
    const auto axis = node->is_nodal() ? 0 : node->get_n() + 1;
    integer_set feasible;
    for(const auto &p : legal)
    {
        bool matches = true;
        for(auto *prefix = node; !prefix->is_nodal(); prefix = prefix->get_parent())
            matches &= p[prefix->get_n()] == prefix->get_i();
        if(matches) feasible.insert(p[axis]);
    }
    const auto existing = node->get_children().size();
    for(std::size_t i = 0; i < existing; ++i)
    {
        auto *child = node->get_children()[i];
        assert(feasible.erase(child->get_i()));
        check_changing_order_prefixes(child, legal, random_order);
    }
    std::size_t iteration = 0;
    while(!feasible.empty())
    {
        const bool use_random = iteration++ % 2 == 0;
        auto expected = use_random ? *random_order(axis, feasible).begin()
                                   : *natural_HC_ordering{}(axis, feasible).begin();
        auto *child = use_random ? node->expand_next(random_order)
                                 : node->expand_next(natural_HC_ordering{});
        assert(child && child->get_i() == expected);
        assert(feasible.erase(expected));
        check_changing_order_prefixes(child, legal, random_order);
    }
    assert(!node->expand_next(random_order));
}

void test_shared_ordered_search()
{
    const auto position = state::from_pgn(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n");
    auto [info, initial] = HC_info::build_HC(position);
    const auto legal = legal_completions(info, initial);
    assert(!legal.empty());
    std::vector<std::vector<float>> scores(info.dimension());
    for(index_t n = 0; n < info.dimension(); ++n)
    {
        const auto maximum = *std::max_element(info.get_universe()[n].begin(), info.get_universe()[n].end());
        scores[n].resize(maximum + 1);
        for(auto coordinate : info.get_universe()[n]) scores[n][coordinate] = float(coordinate % 7);
    }
    std::mt19937 random(91);
    scored_HC_ordering order(info.get_universe(), scores, random);
    for(auto builder : {fine_builder::regular, fine_builder::solid})
    for(bool remove_check : {false, true})
    {
        fine_search_options options;
        options.builder = builder;
        options.build_options.remove_check = remove_check;
        auto root = test_node::make_root(position, {}, options);
        // Abandon and resume generators, interleaving siblings while their
        // constraints and failure records live in the shared cell forest.
        auto first = root->search(order).first();
        assert(first);
        auto *child = root->get_child(*first);
        if(!child->is_ceiling()) child->search(order).first();
        auto second = root->search(order).first();
        if(second && !root->get_child(*second)->is_ceiling())
            root->get_child(*second)->search(order).first();
        check_ordered_prefixes(root.get(), legal, order);
        auto *ceiling = finetree_nearby_ceiling(root.get());
        assert(ceiling && position.can_apply(action::from_moveseq(ceiling->to_action(), position)));
        ceiling->ignite();
        assert(ceiling->get_search_options().builder == builder);
        assert(ceiling->get_search_options().build_options.remove_check == remove_check);
        const auto *context = &ceiling->get_hc_info();
        ceiling->ignite();
        assert(&ceiling->get_hc_info() == context);
    }
    std::mt19937 changing_rng(119);
    random_HC_ordering changing_order(info.get_universe(), changing_rng);
    auto changing_root = test_node::make_root(position);
    check_changing_order_prefixes(changing_root.get(), legal, changing_order);
}

void test_budgeted_prefix_search()
{
    const auto position = state::from_pgn(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n");
    auto [info, initial] = HC_info::build_HC(position);
    const auto legal = legal_completions(info, initial);
    const auto fallback = info.first_completion(info.get_universe());
    assert(fallback && info.dimension() > 1);
    const auto alternative = std::find_if(legal.begin(), legal.end(), [&](const point &p) {
        return p[0] != (*fallback)[0];
    });
    assert(alternative != legal.end());

    std::vector<std::vector<float>> scores(info.dimension());
    for(index_t axis = 0; axis < info.dimension(); ++axis)
        scores[axis].resize(*std::ranges::max_element(info.get_universe()[axis]) + 1);
    scores[0][(*alternative)[0]] = 2;
    scores[0][(*fallback)[0]] = 1;
    std::mt19937 random(91);
    const scored_HC_ordering order(info.get_universe(), scores, random);

    auto strict = test_node::make_root(position);
    assert(strict->expand_next(order)->get_i() == (*alternative)[0]);

    // One unit spends the budget during discovery. Full width must adopt
    // the ordinary completion even though the preferred semimove is legal.
    auto root = test_node::make_root(position, {}, fine_search_options{.prefix_budget = 1});
    auto *child = root->expand_next(order);
    assert(child && child->get_i() == (*fallback)[0]);
    assert(finetree_nearby_ceiling(child)->to_action() == info.to_action(*fallback));

    // The timed-out choice stays available and the materialized suffix still
    // admits every other legal continuation.
    std::set<moveseq> expected, actual;
    for(const auto &p : legal) expected.insert(info.to_action(p));
    collect_tree_actions(root.get(), order, actual);
    assert(actual == expected);
}

int main()
{
    test_budgeted_prefix_search();
    test_shared_ordered_search();
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
