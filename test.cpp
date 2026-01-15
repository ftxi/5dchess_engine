#include "state.h"
#include "pgnparser.h"
#include <tuple>
#include <ranges>
#include <cassert>

std::string str = R"(
[Board "custom"]
[Mode "5D"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:0:b]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:1:w]

1.(0T1)Ng1f3 / (0T1)e7e6 
2.(0T2)b2b3 / (0T2)c7c6 
3.(0T3)e2e3 / (0T3)Qd8b6 
4.(0T4)Nf3g5 / (0T4)Qb6>>(0T0)f2
5.(-1T1)Ke1f2 / (-1T1)Ng8f6 
6.(-1T2)e2e3 / (-1T2)Nf6>>(-1T1)f4 
7.(-1T3)Kf2e1 / (-1T3)Ke8>>(0T4)d8 
8.(-1T4)Qd1f3 / (-1T4)f7f6 
9.(0T5)Qd1f3 (-1T5)Ng1h3
)";

int main()
{
    
    state s(*pgnparser(str).parse_game());
    std::cout << s.to_string();
    bool c = s.apply_move(full_move("(0T5)Ke8>>(0T4)d8"));
    assert(c);
    vec4 p = vec4(5,5,5,-1);
    std::cout << s.get_piece(p, 1) << "\n";
    for(vec4 q : s.gen_piece_move(p))
    {
        std::cout << s.pretty_move<state::SHOW_ALL>(full_move(p, q)) << "\n";
    }
    return 0;
}
