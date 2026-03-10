#include <iostream>
#include <string>
#include "state.h"
#include "hypercuboid.h"
#include "pgnparser.h"

const std::string ultra_wide = R"(
[Board "Standard - Turn Zero"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:0:b]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:1:w]


1. Nf3 / N>>g6 
2. (L0)N>>f5 (L-1)N>>g3 / (L-1)N>>e6 (L1)N>>g6 (L2)N>>g6 
3. (L-3)Ng5 (L-2)Ng5 (L-1)h4 (L1)Nxg7+ (L2)Nh5 / (L1)Bxg7 (-2T2)N>(-3T2)c6 (2T2)N>>(2T1)b6 (0T2)N>>(0T1)b6 (-1T2)N>>(0T2)g6 
4. (L-4)N>>xf7 (L-5)N>>b3 (L-6)N>>f5 (L-3)N>>xg7+ (-2T3)N>>(-1T1)g5 (L1)N>>g3 (L2)N>>g3 / (7T1)e6 
5. (7T2)Nc3 / (L8)Rg8 (7T2)N8e7 (L6)Bxg7 (L5)Nh6 (L4)Nh6 (L3)Nh6 (L-4)Nh6 (L-5)Nh6 (L-6)Nh6 
6. (L-6)d3 (L-4)B>b1 (L-1)Nc3 (5T3)N>(6T3)g3 (L8)e3 (L-7)B>>g1 (0T3)N>>(1T3)b3 (L3)N>>b3 (L4)B>>g1 (7T3)N1>>(7T2)g3 / (L14)Qxg5 (L13)Nxf7 (L10)Ng4 (L8)g6 (L9)B>xg5 (L11)B>xg2 (L5)N>b4 (L2)B>xf7 (L12)B>>xf7 (1T3)B>>(1T1)g5 
7. (-9T2)Nxg7+ / Bxg7 
8. (-9T3)h4 / (L0)Rg8 (L-1)Rg8 (L-2)Nh6 (L-3)Nh6 (L-4)Rg8 (L-5)Rg8 (L-6)Rg8 (L-7)Rg8 (L-9)Nh6 
9. (L-9)h5 (L-8)Ng5 (L-7)h4 (L-6)d4 (L-5)Nh5 (L-4)h4 (L-3)h4 (L-2)h4 (L-1)h5 (L0)h4 (L1)h4 (L2)Nxg7+ (L3)Ng5 (L4)Nxg7+ (L5)Nxg7+ (L6)Nh5 (L7)Nd5 (L8)Ng7+ (L9)Nh5 (L10)h4 / (L-9)B>>xg7 (L7)B>>xg2 
10. Nxh7 / (-11T2)Bxf1+ 
11w. (L-11)Kxf1 (L-10)g>>xg2 / (L15)e6 (-7T4)N>>(-6T2)g6 (-5T4)N>>(-4T2)b6 (L3)B>>f6 (L-11)N>>e6 (6T4)N8>>x(6T2)g7 
12. (L-16)Ng5 (L-15)Nxf8 (L-13)h4 (L-12)h4 (L14)Nh5 (L15)Nxh7 / (L15)Rxh7 (14T3)N8e7 (L-10)Nh6 (L-12)Nh6 (L-13)Nh6 (L-15)Kxf8 (L-16)Nh6 
13. (L-16)Nxh7 (L-15)h4 (L-14)Nxh8+ (L-13)h5 (L-12)h5 (L-11)Nxf8+ (L-10)h4 (L11)Nc5 (L12)Nxg7+ (L13)Ng5 (L14)Nxg7+ (L15)h4 / (15T4)N8e7 (L14)Bxg7 (L13)Rg8 (L12)Kf8 (L11)Rg8 (L10)Rg8 (L9)Rg8 (L8)Rxg7 (L5)Bxg7 (L4)Bxg7 (L2)Kf8 (L1)Rg8 (L0)Rh8 (L-1)Rh8 (L-2)Rg8 (L-3)Rg8 (L-4)Rh8 (L-6)Rh8 (L-8)Rg8 (L-10)Rg8 (L-11)Kxf8 (L-12)Rg8 (L-13)Rg8 (L-14)Nxh8 (L-15)Rh7 (L-16)Rg8 
)";

int main()
{
    state s(*pgnparser(ultra_wide).parse_game());
    auto [w, ss] = HC_info::build_HC(s);
    w.shuffle(ss);

    auto gen = w.search(ss);
    int printed = 0;
    for(const moveseq &mvs : gen)
    {
        std::vector<ext_move> emvs;
        emvs.reserve(mvs.size());
        for(const full_move &fm : mvs)
        {
            emvs.emplace_back(fm);
        }
        action act = action::from_vector(emvs, s);
        std::cout << s.pretty_action(act) << '\n';
        if(++printed >= 1000)
        {
            break;
        }
        // test if the action is legal
        state tmp = s;
        bool ok = true;
        for(const full_move &fm : mvs)
        {
            if(!tmp.apply_move(fm))
            {
                std::cerr << "apply_move failed for "
                          << tmp.pretty_move<state::SHOW_NOTHING>(fm)
                          << "\n";
                ok = false;
                break;
            }
        }
        if(!ok)
        {
            continue;
        }
        if(!tmp.submit())
        {
            std::cerr << "submit failed for action" << '\n';
            continue;
        }
    }
    std::cout << "-= random_action.cpp: all tests passed =-\n";
    return 0;
}