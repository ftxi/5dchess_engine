#include "position_tools.h"

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include "pgnparser.h"
#include "search_tools.h"

namespace
{
bool help_requested(int argc, const char *argv[])
{
    return argc == 2 && (std::string_view(argv[1]) == "-h"
                         || std::string_view(argv[1]) == "--help");
}

void print_position_help(
    std::ostream &out,
    std::string_view command,
    std::string_view arguments,
    std::string_view description)
{
    out << "Usage: 5dtools " << command;
    if(!arguments.empty()) out << ' ' << arguments;
    out << "\n  " << description << "\n"
        << "  Reads a 5DPGN game from stdin.\n"
        << "  -h, --help  display this help text and exit\n";
}

bool validate_arguments(
    int argc,
    int maximum,
    std::string_view command,
    std::string_view arguments,
    std::string_view description)
{
    if(argc <= maximum) return true;
    std::cerr << "Error: too many arguments\n";
    print_position_help(std::cerr, command, arguments, description);
    return false;
}

struct parsed_position
{
    std::unique_ptr<state> value;
};

std::optional<parsed_position> read_position(int &exit_code)
{
    std::ostringstream input;
    input << std::cin.rdbuf();
    std::string pgn = input.str();
    try
    {
        auto game = pgnparser(pgn).parse_game();
        return parsed_position{std::make_unique<state>(*game)};
    }
    catch(const parse_error &error)
    {
        std::cerr << "Parse Error: " << error.what() << '\n';
        exit_code = 2;
    }
    catch(const std::runtime_error &error)
    {
        std::cerr << "Runtime error: " << error.what() << '\n';
        exit_code = 1;
    }
    return std::nullopt;
}

template<typename Function>
int with_position(Function function)
{
    int exit_code = 0;
    auto position = read_position(exit_code);
    if(!position)
    {
        return exit_code;
    }
    function(*position->value);
    return 0;
}
}

int run_print(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "print", "", "Print the final state of a 5DPGN game.");
        return 0;
    }
    if(!validate_arguments(argc, 1, "print", "", "Print the final state of a 5DPGN game.")) return 2;
    return with_position([](const state &s) { std::cout << s.to_string(); });
}

int run_count(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "count", "[policy] [max]", "Count available actions (default max: 10000).");
        return 0;
    }
    if(!validate_arguments(argc, 3, "count", "[policy] [max]", "Count available actions (default max: 10000).")) return 2;
    search_mode mode;
    int max;
    try { std::tie(mode, max) = parse_search_args(argc, argv); }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search arguments\n";
        print_position_help(std::cerr, "count", "[policy] [max]", "Count available actions (default max: 10000).");
        return 2;
    }
    return with_position([&](state s) {
        switch(mode)
        {
            case search_mode::balanced: count_balanced(s, max); break;
            case search_mode::naive: count_naive(s, max); break;
            case search_mode::stable: count_stable(s, max); break;
            case search_mode::iterative: count_iterative(s, max); break;
            case search_mode::mixed: count_mixed(s, max); break;
        }
    });
}

int run_all(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "all", "[policy] [max]", "Print available actions (default max: 10000).");
        return 0;
    }
    if(!validate_arguments(argc, 3, "all", "[policy] [max]", "Print available actions (default max: 10000).")) return 2;
    search_mode mode;
    int max;
    try { std::tie(mode, max) = parse_search_args(argc, argv); }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search arguments\n";
        print_position_help(std::cerr, "all", "[policy] [max]", "Print available actions (default max: 10000).");
        return 2;
    }
    return with_position([&](state s) {
        switch(mode)
        {
            case search_mode::balanced: count_balanced<true>(s, max); break;
            case search_mode::naive: count_naive<true>(s, max); break;
            case search_mode::stable: count_stable<true>(s, max); break;
            case search_mode::iterative: count_iterative<true>(s, max); break;
            case search_mode::mixed: count_mixed<true>(s, max); break;
        }
    });
}

int run_checkmate(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "checkmate", "[policy]", "Determine whether the final position is checkmate or stalemate.");
        return 0;
    }
    if(!validate_arguments(argc, 2, "checkmate", "[policy]", "Determine whether the final position is checkmate or stalemate.")) return 2;
    search_mode mode;
    int maximum;
    try { std::tie(mode, maximum) = parse_search_args(argc, argv); }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search policy: " << argv[1] << '\n';
        print_position_help(std::cerr, "checkmate", "[policy]", "Determine whether the final position is checkmate or stalemate.");
        return 2;
    }
    (void)maximum;
    return with_position([&](state s) {
        const auto [present, player] = s.get_present();
        (void)present;
        if(auto moves = find_first_action(s, mode))
        {
            std::cout << "Not checkmate: ";
            for(const full_move &move : *moves)
            {
                std::cout << move.pgn(s, QUEEN_W, pgn_options::SHOW_CAPTURE) << ' ';
                s.apply_move(move);
            }
        }
        else if(s.phantom().find_checks(!player).first())
        {
            std::cout << "Checkmate";
        }
        else
        {
            std::cout << "Stalemate";
        }
        std::cout << '\n';
    });
}

int run_diff(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "diff", "", "Compare balanced and naive search results.");
        return 0;
    }
    if(!validate_arguments(argc, 1, "diff", "", "Compare balanced and naive search results.")) return 2;
    return with_position([](state s) { diff(s); });
}
