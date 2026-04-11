#include "state.h"
#include "pgnparser.h"
#include "hypercuboid.h"

std::string str = R"(
[Mode "5D"]
[Board "Standard"]

1. (0T1)Nc3 / (0T1)a6
2. (0T2)Rb1 / (0T2)a5
3. (0T3)Rb1>>(0T2)b1~ / (1T2)a5 (0T3)Nb8>>(0T2)b6~
)";

int main()
{
    int n = 0;
    state s(*pgnparser(str).parse_game());
    auto [w, ss] = HC_info::build_HC(s);
    for (const auto& _ : w.search(ss))
    {
        n++;
        if(n > 400000)
        {
            return 2;
        }
    }
    if(n != 30134)
    {
        return 1;
    }
    return 0;
}
