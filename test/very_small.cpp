#include "game.h"
#include <tuple>

std::string very_small_open =
//R"(
//[Size "4x4"]
//[Board "custom"]
//[Mode "5D"]
//[nbrk/3p*/P*3/KRBN:0:1:w]
//
//1. (0T1)Rb1xb4 / (0T1)Rc4xb4 
//2. (0T2)Bc1>>(0T1)c2 / (1T1)d3d2 
//3. (1T2)a2a3
//)";
R"(
[Board "Standard"]
1.d3 / c6
2.Nf3 / (0T2)d8a5
)";
int main()
{
    game g = game::from_pgn(very_small_open);
    std::cout << g.get_match_status() << "\n";
    g.apply_move(full_move("(0T3)e1d2"));
    
    std::cout << g.get_current_state().to_string();
    for(auto [p,q] :g.get_current_checks())
    {
        std::cout << "check path: " << full_move(p, q) << "\n";
    }
//    ext_move m(vec4(0,1,1,0), vec4(0,2,1,0));
//    g.apply_move(m);
//    g.submit();
//    full_move fm0("(0T2)Rb4b1"), fm1("(1T2)Rc4c2");
//    ext_move em0(fm0), em1(fm1);
//    action a = action::from_vector({em0, em1}, g.get_current_state());
//    action b = action::from_vector({em1, em0}, g.get_current_state());
//    std::cout << a << "\n";
//    std::cout << b << "\n";
//    g.apply_move(em0);
//    g.apply_move(em1);
//    g.submit();
//    g.visit_parent();
//    g.apply_move(em1);
//    g.apply_move(em0);
//    g.submit();
//    std::cout << g.show_pgn() << "\n";
//    g.suggest_action();
//    g.suggest_action();
//    bool flag = g.suggest_action();
//    std::cout << flag << "\n";
//    for(auto & [act, txt] : g.get_child_actions())
//    {
//        std::cout << txt << " i.e. ";
//        std::cout << g.get_current_state().pretty_action(act) << "\n";
//    }
    std::cout << g.show_pgn() << "\n";
    return 0;
}
