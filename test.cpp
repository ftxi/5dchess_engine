#include <tuple>
#include <ranges>
#include <cassert>
#include "game.h"

std::string str = R"(
[Board "Standard - Turn Zero"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:0:b]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:1:w]

1. (0T1)e2e4 / (0T1)f7f5 
2. (0T2)Qd1h5+ 
)";

int main()
{
    game g = game::from_pgn(str);
    std::cout << g.show_pgn() << "\n";
    for (auto& [l, t, c, fen] : g.get_phantom_boards_and_checks().first)
    {
        std::cout << "L" << l << " T" << t << " " << (c?"b":"w") << ": " << fen << "\n";
    }
    return 0;
}
