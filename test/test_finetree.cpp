#include <tuple>
#include <ranges>
#include <cassert>
#include "state.h"
#include "pgnparser.h"
#include "finetree.h"
#include "util.h"

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

int main()
{
    state s(*pgnparser(str).parse_game());

    std::cout << s.to_string() << std::endl;
    auto root = fine_node<>::make_root(s);
    fine_node<> *node = root.get();

    // First branch: consume one expansion via search, then navigate to its child
    std::cout << "First branch of root:\n";
    for(index_t i : node->search())
    {
        std::cout << i << ' ';
        node = node->get_child(i);
        break; // only first expansion
    }
    std::cout << "\n\n";
    std::cout << (node->to_string()) << std::endl;

    // Remaining expansions on root
    std::cout << "Other children of root:\n";
    for(index_t i : root->search())
    {
        std::cout << i << ' ';
    }
    std::cout << "\n\n";

    // Search within the first child for more expansions
    std::cout << "Other children of first node:\n";
    for(index_t i : node->search())
    {
        std::cout << i << ' ';
    }
    std::cout << "\n\n";

    std::cout << "Search completed." << std::endl;

    // Navigate to celling, get action, ignite, and repeat
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    node->ignite();

    node->search();  // expand the ignited node
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    node->ignite();

    node->search();
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
