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

fine_node *goto_next_nodal(fine_node *node)
{
    auto children = node->get_children();
    while(children.size()>0)
    {
        node = children[0];
        children = node->get_children();
    }
    assert(node->is_terminal());
    return node;
}

int main()
{
    state s(*pgnparser(str).parse_game());

    std::cout << s.to_string() << std::endl;
    fine_node root(nullptr, s);
    auto ans = root.explore();
    if(!ans)
    {
        std::cout << "No point found." << std::endl;
        return 1;
    }
    
    auto [pt, cell, hc] = *ans;
    std::cout << "cell space: " << cell->space.to_string() << std::endl;
    print_range("Found point:", pt);
    
    std::cout << root.to_string() << std::endl;
    
//    fine_node *final_node = root.isolate(pt, cell, hc);
//    std::cout << "Isolate completed." << std::endl;
//    std::cout << root.to_string() << std::endl;
//    
//    root.normalize(pt, cell, final_node);
//    std::cout << "Normalize completed." << std::endl;
//    std::cout << root.to_string() << std::endl;
//    
    fine_node *node = root.expand();
    std::cout << "Expand completed." << std::endl;
    std::cout << (node->to_string()) << std::endl;
    
    std::cout << "Other children of root:\n";
    for(index_t i : root.search())
    {
        std::cout << i << ' ';
    }
    std::cout << "\n\n";
    
    std::cout << "Other children of first node:\n";
    for(index_t i : node->search())
    {
        std::cout << i << ' ';
    }
    std::cout << "\n\n";

    std::cout << "Search completed." << std::endl;
    
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    node->ignite();
    
    node->expand();
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    node->ignite();
    
    node->expand();
    node = node->expand();
    node = goto_next_nodal(node);
    print_range("Got action: ", node->to_action());
    return 0;
}
