#include <tuple>
#include <ranges>
#include <cassert>
#include "game.h"

std::string str = R"(
[Mode "5D"]
[Board "Standard"]
1. e3 / Nf6
2w. Bb5
2b. c6
3. c3 / cxb5
4. Qb3 / Qa5
5. Q>>xf7 / (1T1)Kxf7
6. (1T2)Nh3 / (1T2)e6
7. (1T3)e3 / (1T3)Qf6
8. (1T4)Qh5 / (0T5)Qa5>>(0T1)a5 
9. (-1T2)e3e4 / (-1T2)Ng8h6
10w. (-1T3)Bf1c4 / (-1T3)Rh8g8 
)";

int main()
{
    game g = game::from_pgn(str);
    g.get_cached_moves();
    g.apply_move(ext_move(full_move("(-1T4)Bc4f7")));
    g.undo();
    g.get_cached_moves();
    std::cout << g.show_pgn() << "\n";
//    std::cout << static_cast<int>(g.get_current_state().get_mate_type()) << "\n";
//    for(const auto &[act,txt] : g.get_historical_actions())
//    {
//        std::cout << txt << "\n";
//    }
    return 0;
}
