#include "state.h"
#include "pgnparser.h"
#include "hypercuboid.h"
#include <iostream>

std::string str = R"(
[Board "Standard - Turn Zero"]

1. Nf3 / e6 
2. b3 / c6 
3. e3 / Qb6 
4. Ng5 / Q>>xf2+ 
5. Kxf2 / Nf6 
6. e3 / (-1T2)N>>(-1T1)f4+ 
7. (-1T3)Ke1 / K>>d8 
8. (-1T4)Qf3 / f6 
9. (L-1)Nh3 (L0)Qf3 / (L0)K>>d8 (L-1)f>>f6 
10. (L-1)Qf7 (L0)Qxf7 / (L0)B>xf7+ 
)";

int main()
{
    int n = 0;
    state s(*pgnparser(str).parse_game());
    auto [w, ss] = HC_info::build_HC(s);
    for (const auto& _ : w.search(ss))
    {
        (void)_;
        n++;
        if(n > 400000)
        {
            return 2;
        }
    }
    if(n != 55332)
    {
        std::cerr << n << std::endl;
        return 1;
    }
    return 0;
}
