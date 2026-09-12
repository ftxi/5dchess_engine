#undef NDEBUG
#include <cassert>
#include <iostream>
#include <vector>
#include "pgnparser.h"
#include "utils.h"
#include "state.h"
#include "vec4.h"
#include "variants.h"

void test_board_coordinate_limits()
{
    const std::string fen = "8/8/8/8/8/8/8/8";
    auto parse = [&](const std::string &coordinates) {
        return pgnparser::parse_board_fen_metadata(fen + coordinates);
    };
    auto rejected = [&](const std::string &coordinates) {
        try
        {
            parse(coordinates);
            return false;
        }
        catch(const parse_error &)
        {
            return true;
        }
    };

    assert(std::get<3>(parse(":0:" + std::to_string(vec4::T_MAX) + ":w"))
           == vec4::T_MAX);
    assert(rejected(":0:" + std::to_string(vec4::T_MAX + 1) + ":w"));
    assert(rejected(":0:-1:w"));
    assert(std::get<2>(parse(":" + std::to_string(vec4::L_MAX) + ":0:w"))
           == vec4::L_MAX);
    assert(std::get<1>(parse(":-" + std::to_string(vec4::L_MAX) + ":0:w"))
           == pgnparser_ast::NEGATIVE);
    assert(rejected(":" + std::to_string(vec4::L_MAX + 1) + ":0:w"));
    assert(rejected(":-" + std::to_string(vec4::L_MAX + 1) + ":0:w"));
    assert(rejected(":0:5000:w"));
}

void test_short_board_rejected()
{
    const std::string input = R"(
[Size "8x8"]
[Board "Custom"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*:0:0:w]

1-0
)";
    try
    {
        state(*pgnparser(input).parse_game());
        assert(false);
    }
    catch(const parse_error &)
    {
    }
}

void test_timeline_header()
{
    const std::string boards = R"(
[Board "Custom"]
[Size "8x8"]
[8/8/8/8/8/8/8/K6k:0:0:w]
)";
    auto setup = [&](const std::string &value) {
        auto game = pgnparser("[Timeline \"" + value + "\"]\n" + boards).parse_game();
        return derive_variant_setup(*game);
    };

    assert(setup("Even").is_even_timelines);
    assert(!setup("odd").is_even_timelines);
    try
    {
        setup("Sideways");
        assert(false);
    }
    catch(const parse_error &)
    {
    }
}

void test_actions()
{
    pgnparser("1.{} {{ab {c}}d}\n / (2.{1987yhwqsi}))").test_lexer();
    std::cout << "void\n";
    std::cout << pgnparser("$(L+7T-6)").parse_relative_board() << "\n";
    std::cout << pgnparser("(L+7T6)").parse_absolute_board() << "\n";
    std::cout << pgnparser("(T5)").parse_absolute_board() << "\n";
    std::cout << pgnparser("(L+5T7)").parse_absolute_board() << "\n";
    std::cout << pgnparser("(2. Kf7)", false).parse_absolute_board() << "\n";
    std::cout << pgnparser("(16b.Qxg5 17.", false).parse_absolute_board() << "\n";
    std::cout << pgnparser("16b.Qxg5 17.", false).parse_actions() << "\n";
    std::cout << pgnparser(R"(16b.(-2)Kxc3{}(-1)Qxg5    (0T9)f3 {bloody}{gibberish} (1T7)e2>>$(T-1)b1+~??? {%command1%}
{new-line {nested}}
 
 17.)", false).parse_actions() << "\n";
    std::cout << pgnparser("16b. R5g6 c3(17.Ke5)", false).parse_actions() << "\n";
    
    std::cout << pgnparser("(L+7T6)Q>>a3+~?!!(~T5)(>L-10)").parse_move() << "\n";
//    std::cout << pgnparser("16b. R5g6 c3>>(17.Ke5)", false).parse_actions() << "\n"; //error
//    std::cout << pgnparser("c3>>(17.Ke5)", false).parse_move() << "\n"; //error
}

void test_gametree()
{
    std::string str=R"(1w.e3 ( 1b. e6 {f7-sac}2.f3 / Nf6 3.Bb5 / Pc6
)(1b. c6 {branch2}) 1b.Nf6 {main} 2.Nf3 / d5 3.c4 / c6 4.cxd5/Qxd5
)";
    //std::string str = "1w.e3 (1b.e6 2.f3/Nf6) (1b. c6 {falsch}) 1b.Nf6";
    std::cout << pgnparser(str).parse_gametree() << "\n";
//    std::cout << pgnparser("1w. e3 (1b.e6) 1b.Nf6 {main} 2.Nf3 / d5 3.c4 / c6 4.cxd5/Qxd5", true, {1,false}).parse_gametree() << "\n";
}

void test_game()
{
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
8. (1T4)Qh5*
)";
    std::string str2 = R"(
[Size "4x4"]
[Board "Custom"]
[Mode "5D"]
[nbrk/3p*/P*3/KRBN:0:1:w]
)";
    pgnparser_ast::game g = *pgnparser(str2).parse_game();
    std::cout << g << "\n\n" << std::endl;
    state s(g);
    std::cout << s.to_string();
}

int main()
{
    //test_actions();
    //test_gametree();
    test_game();
    test_board_coordinate_limits();
    test_short_board_rejected();
    test_timeline_header();
    std::cout << "---= parse_game.cpp: all tests passed =---" <<std::endl;
    return 0;
}
