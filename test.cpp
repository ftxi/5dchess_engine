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

1. Bb2 / Nxb2
)";

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
    
    fine_node *final_node = root.isolate(pt, cell, hc);
    std::cout << "Isolate completed." << std::endl;
    std::cout << root.to_string() << std::endl;
    
    root.normalize(pt, cell, final_node);
    return 0;
}
