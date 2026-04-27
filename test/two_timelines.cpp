#include "game.h"

std::string str = R"(
[Mode "5D"]
[Board "Custom - Even"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:+0:1:w]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:-0:1:w]
(1.(L-0)Nf3 (L+0)e3 / (L-0)Nf6 {discard} (L+0)Nf6)
1.(L-0)e3 (L+0)N>>g3 {this is branching!}
)";

int main()
{
    game g = game::from_pgn(str);
    auto guard = [](bool x){ if(!x) exit(1);};
    guard(g.suggest_action());
    guard(g.suggest_action());
    std::cout << g.show_pgn() << "\n";
    return 0;
}
