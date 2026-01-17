#include "game.h"
#include <tuple>
#include <ranges>

std::string just_unicorns =
R"(
[Board "Custom - Odd"]
[Mode "5D"]
[Size "5x5"]
[1u1uk*/5/5/5/K*U1U1:0:1:w]

1. Kb2 / Kd4
2. Kc1 / Kc5 {gugugaga}
)";
int main()
{
    game g = game::from_pgn(just_unicorns);
    state s = g.get_current_state();
    std::cout << s.to_string();
    
    for(int i=0; i < 10; i++)
        g.suggest_action();
    bool flag = g.suggest_action();
    std::cout << flag << "\n";
    auto comments = g.get_comments();
    print_range("comments:", comments);
    comments.push_back("ohyeah");
    g.set_comments(comments);
    std::cout << g.show_pgn() << "\n";
    return 0;
}

