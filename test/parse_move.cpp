#include <iostream>
#include <vector>
#include "pgnparser.h"
#include "utils.h"

void test_match()
{
    std::vector<std::tuple<std::string, std::string, bool>> tests = {
        {"(L0T0)e2e4", "(0T0)Pe2e4", true},
        {"(L0T0)e2e4", "(+0T0)Pe2e4", false},
        {"a8=Q", "(7T9)b7xa8=Q", true},
        {"a8Q", "(7T9)b7xa8=Q", true},
        {"b7a8R", "(7T9)Pb7xa8=R", true},
        {"O-O", "(0T9)Ke8g8", true},
        {"O-O-O", "(8T9)O-O", false},
        {"(T8)O-O-O", "(0T8)O-O-O", true},
        {"Q>>xf7", "(0T10)Qf3>>x$(T-5)f7", true},
        {"Q>>xf7N", "(0T10)Qf3>>x$(T-5)f7=N", true},
        {"Q>>xf7", "(0T10)Qf3>x(0T1)f7", false},
        {"(L+0T4)B>>(L-0)a4", "(L+0T4)Ba4>>x(L-0T3)a4", true},
        {"(L+0T4)B>>x(L-0)a4", "(L+0T4)Ba4>>(L-0T3)a4", false},
        {"(-1T10)Qc7>(1T10)e5", "(0T13)Qa3>>(0T9)e7", false},
        {"(-1T10)Qc7>(1T10)e5", "(-1T10)Qc7>x(1T10)e5", true},
        {"(-1T10)Qc7>(1T10)e5", "(-1T10)Qc7>(1T10)a5", false},
        {"(0T8)Qd8b6", "(0T8)Qd8d6", false},
    };
    for(auto [simple, full, expected] : tests)
    {
        auto obj_simple = pgnparser(simple).parse_move();
        auto obj_full = pgnparser(full).parse_move();
        std::cout << "simple: " << simple << "\nfull: " << full << "\n";
        std::cout << "parsed simple: " << obj_simple << "\nparsed full: " << obj_full << "\n";
        if(obj_simple && obj_full)
        {
            bool match = pgnparser::match_move(*obj_simple, *obj_full);
            std::cout << "match result: " << (match ? "true" : "false") << ", expected: " << (expected ? "true" : "false") << "\n\n";
            if(match != expected)
            {
                std::cout << "unexpected match/mismatch\n\n";
                return;
            }
        }
        else
        {
            std::cout << "parsing failed\n\n";
            return;
        }
    }
    std::cout << "-= test_match(): all tests finished =-\n";
}



int main()
{
    test_match();
//    auto obj = pgnparser("(-1T5)f6>>(0T5)f6").parse_superphysical_move();
//    std::cout << obj << "\n";
    return 0;
}
