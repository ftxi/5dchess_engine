#include <tuple>
#include <ranges>
#include <cassert>
#include "game.h"

std::string str = R"(
[Board "Standard"]
[Mode "5D"]
1. Nf3
/ Nc6
2. (0T2)Nf3>>(0T1)f5 (~T2) (>L1)
/ (0T2)Nc6>>(0T1)c4 (~T1) (>L-1) (1T1)Nb8>>(0T1)b6 (~T3) (>L-2)
3. (-2T2)Nf3>>(0T1)f3 (~T4) (>L2)
/ (2T1)Ng8>>(1T1)g6 (~T5) (>L-3)
4. (2T2)Ne5 (-3T2)Nxg7 (-1T2)e3 (1T2)e3
/ (-3T2)Bxg7 (-2T2)e6 (-1T2)e6 (1T2)e6 (2T2)e6
5. (1T3)Qf3 (-3T3)Nf3 (0T3)Nc3 (-1T3)Bd3 (-2T3)e3 (2T3)e3
/ (-2T3)Qf6 (-3T3)c5 (-1T3)Qh4 (2T3)Qh4 (1T3)Bb4 (0T3)c6
6. (1T4)Qf3>(2T4)e4 (-2T4)Qh5 (-3T4)Ng5 (0T4)e3 (-1T4)b3
/ (1T4)e5 (-1T4)Qh4>>x(-1T2)f2 (>L-4) (-2T4)Qf6>(-3T4)e6 (2T4)Bb4 (0T4)Nf6
7. (0T5)e4 (1T5)Be2 (-1T5)Bg6 (-3T5)e3 (-2T5)Bb5 (2T5)Qh5
/ (-2T5)Bc5 (-1T5)Nxe3 (-3T5)Qa5 (2T5)Bxd2 (0T5)Ng4 (1T5)Qg5
8. (-1T6)fxe3 (-3T6)Nxe6 (1T6)g3 (-2T6)Qh5>>(0T4)h5 (~T7) (>L3) (-4T3)Kxf2
/ (-4T3)e6 (3T4)g6
9. (-4T4)Ke1
/ (-4T4)Qg5
10. (3T5)Nd5 (-4T5)Nd4
/ (3T5)Qa5 (-4T5)Bb4
11. (2T6)Bxd2 (3T6)Qe5 (0T6)Bc4 (-4T6)Qh5
/ (3T6)Qxd2 (-3T6)dxe6 (-4T6)Nxe3 (-2T6)Bxe3 (0T6)Ne3 (-1T6)Bb4 (1T6)Qxe3 (2T6)Qxf2
12. (0T7)dxe3 (-1T7)c3 (3T7)Qxd2 (-2T7)dxe3 (-3T7)Qd1>>(-3T5)f3 (~T8) (>L4)
/ (4T5)Bc3
13. (4T6)Nxe6
/ (4T6)dxe6
14. (4T7)Qf3>>x(0T3)f7 (~T10) (>L5)
/ (5T3)Kxf7
15. (5T4)e3
/ (5T4)Nf6
16. (5T5)Qf3
/ (5T5)e6
17. (5T6)Qe4
/ (-3T7)Qa5>>x(-3T4)d2 (~T9) (>L-5)
18. (-5T5)Qxd2
/ (-5T5)Bd4
19. (-5T6)Ne6
/ (-2T7)Nb6>>x(-2T5)b5 (~T11) (>L-6)
20. (-6T6)Qxf7
/ (5T6)Nxe4 (-5T6)dxe6 (-6T6)Kxf7
21. (-6T7)Bc1>>(-6T6)d1 (~T12) (>L6)
/ (6T6)Bb4
22. (-2T8)Bb5>>(-2T6)d5 (~T14) (>L7)
/ (-6T7)Nb6>>(-5T5)b6
23. (-7T6)Qd2d5
/ (-7T6)Nb6>>(-5T5)b6
24. (-8T6)Qd2d5
)";

int main()
{
    game g = game::from_pgn(str);
    std::cout << g.show_pgn() << "\n";
    std::cout << static_cast<int>(g.get_current_state().get_mate_type()) << "\n";
    return 0;
}
