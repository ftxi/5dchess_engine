#include "state.h"
#include "pgnparser.h"
#include <tuple>
#include <ranges>
#include <cassert>

std::string str = R"(
[Mode "5D"]
[Board "Standard"]
1. e3 / Nf6
2w. Bb5 {Beware!}
(2b. d5 {The right response})
2b. c6
3. c3 / cxb5
4. Qb3 / Qa5
5. Q>>xf7+~ (~T1) (>L1) {f7-sacrifice!} / (1T1)Kxf7
6. (1T2)Nh3 / (1T2)e6
7. (1T3)e3 / (1T3)Qf6
8. (1T4)Qh5* / (0T5)Qa5>>(0T1)a5 
9. (-1T2)e3e4 / (-1T2)Ng8h6 
(10. (-1T3)e4e5 / (-1T3)Rh8g8 
11. (-1T4)e5e6 / (-1T4)Nh6>x(1T4)h5 {recapture}
12. (-1T5)e6xf7 (1T5)Nh3g5 )
10. (-1T3)Bf1c4 / (-1T3)Rh8g8 
)";

int main()
{
    state s(*pgnparser(str).parse_game());
    std::cout << s.to_string();
    // bool c = s.apply_move(full_move("(1T4)Qd1h5"));
    // assert(c);
    full_move fm("(-1T4)Bc4f7");
    std::cout << s.pretty_move<state::SHOW_ALL>(fm) << "\n";
    action act = action::from_vector({{fm}}, s);
    std::cout << s.pretty_action<state::SHOW_ALL>(act) << "\n";
    return 0;
}
