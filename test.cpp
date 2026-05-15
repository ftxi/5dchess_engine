#include <tuple>
#include <ranges>
#include <cassert>
#include "state.h"
#include "pgnparser.h"
#include "game.h"
#include "hypercuboid.h"

std::string str = R"(
[Timeline "odd"]
[Size "8x8"]
[Board "custom"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:0:b]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:1:w]

1w.(0T1)a2a3
1b.(0T1)b8c6
2w.(0T2)d2d3
2b.(0T2)d7d5
3w.(0T3)g2g4
3b.(0T3)e8d7
4w.(0T4)c1g5
4b.(0T4)f7f5
5w.(0T5)d1(0T4)d2
5b.(0T5)d7(1T4)e6
6w.(0T6)c2c4 (1T5)g1(0T5)g3
6b.(0T6)g8f6 (1T5)d8(0T5)e8 (2T5)f5g4
7w.(1T6)d2(1T5)e3 (-1T6)g5(1T6)g7 (0T7)g1(0T6)g3 (2T6)g5h4
7b.(-1T6)e8(1T4)g6 (0T7)f6e8 (1T6)c6(0T6)c4 (2T6)a7a6 (3T5)e6(2T5)f7 (4T6)d7(5T6)d6
8w.(3T6)e3(1T6)g3 (2T7)b1(2T6)b3 (1T7)e1(0T7)d2 (-1T7)b1(-1T6)d1 (0T8)g5(0T7)g6 (-2T5)g4g5 (-3T7)g5h6 (-4T6)d1d2 (5T7)g5(4T7)f5
8b.(0T8)f5f4 (1T7)e6(0T8)f7 (-1T7)d8(0T7)d7 (3T6)c6(1T7)c6 (5T7)c8(5T6)b8 (-2T5)g6(-1T6)f6 (-3T7)c4(-1T7)b4 (6T6)g8(5T6)e8 (2T7)d7(1T7)c6 (4T7)e6(5T7)e5 (-4T6)d8(-3T7)d7 (8T7)f6g8 (9T6)e8(8T7)e8 (10T7)d5c4
9w.(2T8)g3(3T6)g3 (0T9)e1(0T8)d1 (1T8)d1(0T9)c1 (3T7)d2(1T7)f4 (-1T8)g1(-1T6)g2 (-2T6)d2(-1T6)c3 (-4T7)g3(-2T6)g3 (-3T8)b1(-1T8)c1 (4T8)d2(5T8)c1 (-5T9)h2h4 (-6T8)f1h3 (-8T7)g3(-9T7)e3 (8T8)b2b3 (9T7)g5e3 (10T8)g5f6 (-11T7)g5d2 (-12T8)h2h3 (-14T8)f2f3 (-15T8)f1g2
9b.(-1T8)c6(1T7)c6 (-4T7)f8(-4T6)e8 (0T9)c8(0T8)b8 (16T6)d7(17T6)e8 (15T6)c6(17T6)c5 (7T6)g8(6T6)g6 (-2T6)f8(3T6)f3 (1T8)g8(2T8)g6 (3T7)d7(3T6)d6 (-3T8)g7g5 (8T8)f8(8T7)g8 (4T8)b7b6 (5T8)c6(5T7)a6 (9T7)f8(8T7)e8 (-5T9)d5c4 (-6T8)h7h6 (10T8)f8(10T7)e8 (-8T7)d8d7 (-9T7)f6b2 (11T6)d5d4 (-12T8)b7b6 (12T8)c6d4 (13T9)e8d6 (-15T8)c6e5 (18T8)c6b8
10w.(16T7)b2b4 (10T9)g6(10T8)f6 (-8T8)f1(-6T8)d1 (-15T9)b1(-15T8)d1 (6T7)d2(5T7)c3 (0T10)b1(0T9)d1 (-1T9)e1(0T9)d1 (-9T8)b1(-9T7)b3 (-2T7)c1(-3T7)d1 (1T9)c2c3 (-22T7)d2f4 (-13T8)g3(-15T9)g3 (2T9)h4g3 (17T7)d2(16T7)d1 (-4T8)g5(-4T7)g6 (4T9)c1(5T8)c1 (-10T8)g1(-9T8)g3 (3T8)f1(4T8)g1 (-6T9)e1(-6T8)d2 (-23T8)d2(-22T7)c3 (-5T10)d3d4 (8T9)d2(8T8)e3 (7T7)d1(8T8)d1 (15T7)g5(17T7)g7 (5T9)g1(4T9)g3 (-24T8)g3(-23T8)e3 (-20T7)g3(-22T7)e5 (-12T9)d1(-13T8)c1 (-7T8)b1(-8T8)b3 (-16T8)f1(-15T9)f1 (-25T8)d2(-24T8)d1 (11T7)d2(10T8)c2 (9T8)e3(8T8)f3 (12T9)d1(13T10)d2 (-21T7)b1(-20T7)b3 (-26T8)e1(-25T8)d1 (-17T7)g1f3 (-19T7)d2(-22T7)g5 (18T9)g4g5
10b.(11T7)d8(11T6)e8 (-8T8)c8(-6T8)e8 (-4T8)g8(-4T7)g6 (18T9)f8(18T8)e8 (4T9)f8(4T8)e8 (14T7)e6(15T6)e5 (-13T8)c8(-12T8)b8 (7T7)f8(9T7)f6 (-1T9)d7(0T9)d7 (-7T8)g8(-6T8)g6 (6T7)f8(5T7)e8 (2T9)g6(0T9)f6 (-9T8)b2(-9T7)c2 (-6T9)f6(-4T8)f6 (-2T7)c6(-2T6)c4 (-10T8)b4(-12T8)b3 (-5T10)d8(-5T9)d7 (12T9)d4(13T9)b4 (0T10)a7a5 (20T8)f6(18T8)e6 (17T7)d7(18T8)e8 (13T10)c8(13T9)b8 (19T8)c6(20T8)e6 (1T9)c8(0T9)b8 (-14T8)g8(-13T8)g6 (-15T9)e5(-15T8)e3 (3T8)d8(4T8)e8 (15T7)e8(14T7)e8 (9T8)c6(8T8)c4 (16T7)e8(15T7)f7 (8T9)g8f6 (21T8)f6(20T8)h6 (-16T8)e6(-15T9)d7 (10T9)c6(10T8)a6 (-11T7)d8d7 (-17T7)b7b5 (22T7)d6(21T8)e5 (-19T7)c5e6 (-20T7)d7(-19T7)e6 (-21T7)b7b5 (-23T8)c8e6 (23T9)h7h5 (-25T8)e8g6 (25T7)f6d6 (-26T8)f6(-24T8)f7 (26T7)c6b4 (27T9)e5f3 (28T7)e8d7 (30T8)g8f6 (32T8)c6d4 (35T8)e7e5 (36T8)d5d4 (39T8)g7g6 (41T8)d6(42T8)e5 (44T8)a6(46T8)b6 (47T7)g8(45T8)g8 (48T8)f6e4
11w.(7T8)g5(7T7)g4 (-11T8)f1(-12T8)e1 (-13T9)e1(-13T8)d1 (16T8)e1(16T7)d1 (-7T9)d1(-7T8)e1 (4T10)f5(5T9)f5 (2T10)g3(2T9)f3 (-17T8)f3(-15T9)f3 (-32T7)g5e3 (14T8)b1(16T8)b2 (-8T9)g5(-7T9)h5 (-27T7)f1h3 (-41T7)b2b4 (0T11)a1b1 (-1T10)f1(1T10)d1 (-2T8)b1c3 (-3T9)a1a2 (3T9)g1h3 (6T8)f1h3 (-9T9)e1(-10T9)d2 (9T9)b2b4 (10T10)f6e7 (12T10)f2f3 (15T8)c2c3 (-16T9)c1(-15T10)c1 (17T8)c1f4 (-18T9)c4c5 (-19T8)g3e4 (-20T8)f2f3 (-21T8)f1h3 (21T9)d1(19T9)c1 (22T8)a3a4 (-24T9)e1(-23T9)d2 (-25T9)e1(-26T9)e1 (27T10)g2f3 (-28T9)g5c1 (28T8)e1d2 (-31T9)g7(-30T9)g6 (-34T8)e3a7 (35T9)g5h4 (-43T10)e1d2 (44T9)g3(42T9)h3 (-46T9)g1(-45T9)g3 (-49T10)f2f3 (-56T9)g5f6

)";

int main()
{
    std::unique_ptr<state> s = nullptr;
    {
        s = std::make_unique<state>(*pgnparser(str).parse_game());
    }
//    std::cout << static_cast<int>(s->get_mate_type()) << "\n";
    auto [w, ss] = HC_info::build_HC(*s);
    w.shuffle(ss);
    if(auto mv = w.search(ss).first())
    {
        print_range("Not checkmate:", *mv);
    }
    else
    {
        std::cout << "Checkmate." << std::endl;
    }
//    game g = game::from_pgn(str);
//    std::cout << g.show_pgn(state::SHOW_NOTHING) << std::endl;
    return 0;
}
