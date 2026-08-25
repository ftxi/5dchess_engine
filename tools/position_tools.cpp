#include "position_tools.h"

#include <charconv>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "pgnparser.h"
#include "search_tools.h"
#include "utils.h"

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

struct parsed_position
{
    std::unique_ptr<state> value;
};

struct turn_selection
{
    std::size_t actions;
    std::string label;
};

std::string submitted_turn(std::size_t actions)
{
    if(actions == 0) return "0b";
    const bool black = actions % 2 == 0;
    return std::to_string(black ? actions / 2 : (actions + 1) / 2)
        + (black ? "b" : "w");
}

std::optional<turn_selection> parse_turn_selection(
    std::string_view value, std::string &error)
{
    const auto invalid = [&] {
        error = "invalid turn selector '" + std::string(value)
            + "'; expected 0 or a value such as 1w or 10b";
        return std::optional<turn_selection>{};
    };
    if(value == "0" || value == "0b" || value == "0B")
        return turn_selection{0, "0b"};
    if(value.size() < 2) return invalid();
    const char color = value.back();
    if(color != 'w' && color != 'W' && color != 'b' && color != 'B')
        return invalid();
    const std::string_view digits = value.substr(0, value.size() - 1);
    std::size_t number = 0;
    const auto [end, ec]
        = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if(ec != std::errc{} || end != digits.data() + digits.size() || number == 0)
        return invalid();
    const bool black = color == 'b' || color == 'B';
    if(number > std::numeric_limits<std::size_t>::max() / 2)
    {
        error = "turn selector is too large: '" + std::string(value) + "'";
        return std::nullopt;
    }
    const std::size_t actions = number * 2 - (black ? 0 : 1);
    return turn_selection{actions, submitted_turn(actions)};
}

bool parse_at_arguments(
    int argc,
    const char *argv[],
    std::vector<const char *> &positional,
    std::optional<turn_selection> &selection)
{
    positional.push_back(argv[0]);
    for(int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if(arg == "--at")
        {
            if(++i >= argc)
            {
                std::cerr << "Error: --at requires a turn\n";
                return false;
            }
            std::string error;
            auto parsed = parse_turn_selection(argv[i], error);
            if(!parsed)
            {
                std::cerr << "Error: " << error << '\n';
                return false;
            }
            if(selection)
            {
                std::cerr << "Error: --at may only be specified once\n";
                return false;
            }
            selection = std::move(parsed);
        }
        else
        {
            positional.push_back(argv[i]);
        }
    }
    return true;
}

void apply_action(state &current, const pgnparser_ast::actions &act)
{
    for(const auto &mv : act.moves)
    {
        auto [fm, promotion, candidates] = current.parse_move(mv);
        if(!fm)
        {
            std::ostringstream message;
            if(candidates.empty()) message << "Invalid move: " << mv;
            else message << "Ambiguous move: " << mv << "; candidates: "
                         << range_to_string(candidates, "", "");
            throw std::runtime_error(message.str());
        }
        const bool applied = promotion
            ? current.apply_move(*fm, to_white(*promotion))
            : current.apply_move(*fm);
        if(!applied)
        {
            std::ostringstream message;
            message << "Illegal move: " << mv << " (parsed as: " << *fm << ')';
            throw std::runtime_error(message.str());
        }
    }
    if(!current.submit())
    {
        std::ostringstream message;
        message << "Cannot submit after parsing these moves: " << act;
        throw std::runtime_error(message.str());
    }
}

std::optional<parsed_position> read_position(
    int &exit_code,
    const std::optional<turn_selection> &selection = std::nullopt)
{
    std::ostringstream input;
    input << std::cin.rdbuf();
    std::string pgn = input.str();
    try
    {
        auto parsed = pgnparser(pgn).parse_game();
        if(!parsed) throw parse_error("no game found");
        pgnparser_ast::game game = std::move(*parsed);
        if(!selection)
            return parsed_position{std::make_unique<state>(game)};

        pgnparser_ast::gametree mainline = std::move(game.gt);
        game.gt = pgnparser_ast::gametree{};
        auto current = std::make_unique<state>(game);
        std::size_t actions = 0;
        if(selection->actions == 0)
            return parsed_position{std::move(current)};

        auto *node = &mainline;
        while(std::holds_alternative<pgnparser_ast::gametree::variations_t>(
            node->variations_or_outcome))
        {
            const auto &variations
                = std::get<pgnparser_ast::gametree::variations_t>(
                    node->variations_or_outcome);
            if(variations.empty()) break;
            const auto &[action, next] = variations.back();
            apply_action(*current, action);
            ++actions;
            if(actions == selection->actions)
                return parsed_position{std::move(current)};
            node = next.get();
        }

        std::cerr << "Error: turn " << selection->label << " is not available; ";
        if(actions == 0)
            std::cerr << "the game contains no submitted turns (only --at 0 is available)\n";
        else
            std::cerr << "the last submitted main-line turn is "
                      << submitted_turn(actions) << '\n';
        exit_code = 3;
        return std::nullopt;
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
int with_position(
    Function function,
    const std::optional<turn_selection> &selection = std::nullopt)
{
    int exit_code = 0;
    auto position = read_position(exit_code, selection);
    if(!position)
    {
        return exit_code;
    }
    function(*position);
    return 0;
}
}

int run_print(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "print", "[--at turn]", "Print a selected state of a 5DPGN game.");
        return 0;
    }
    std::vector<const char *> positional;
    std::optional<turn_selection> selection;
    if(!parse_at_arguments(argc, argv, positional, selection)) return 2;
    if(positional.size() > 1)
    {
        print_position_help(std::cerr, "print", "[--at turn]", "Print a selected state of a 5DPGN game.");
        return 2;
    }
    return with_position(
        [](const parsed_position &p) { std::cout << p.value->to_string(); },
        selection);
}

int run_count(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "count", "[--at turn] [policy] [max]", "Count available actions (default max: 10000).");
        return 0;
    }
    std::vector<const char *> positional;
    std::optional<turn_selection> selection;
    if(!parse_at_arguments(argc, argv, positional, selection)) return 2;
    if(positional.size() > 3)
    {
        print_position_help(std::cerr, "count", "[--at turn] [policy] [max]", "Count available actions (default max: 10000).");
        return 2;
    }
    search_mode mode;
    int max;
    try { std::tie(mode, max) = parse_search_args(static_cast<int>(positional.size()), positional.data()); }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search arguments\n";
        print_position_help(std::cerr, "count", "[--at turn] [policy] [max]", "Count available actions (default max: 10000).");
        return 2;
    }
    return with_position([&](const parsed_position &position) {
        state s = *position.value;
        switch(mode)
        {
            case search_mode::balanced: count_balanced(s, max); break;
            case search_mode::naive: count_naive(s, max); break;
            case search_mode::stable: count_stable(s, max); break;
            case search_mode::iterative: count_iterative(s, max); break;
            case search_mode::mixed: count_mixed(s, max); break;
        }
    }, selection);
}

int run_all(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "all", "[--at turn] [policy] [max]", "Print available actions (default max: 10000).");
        return 0;
    }
    std::vector<const char *> positional;
    std::optional<turn_selection> selection;
    if(!parse_at_arguments(argc, argv, positional, selection)) return 2;
    if(positional.size() > 3)
    {
        print_position_help(std::cerr, "all", "[--at turn] [policy] [max]", "Print available actions (default max: 10000).");
        return 2;
    }
    search_mode mode;
    int max;
    try { std::tie(mode, max) = parse_search_args(static_cast<int>(positional.size()), positional.data()); }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search arguments\n";
        print_position_help(std::cerr, "all", "[--at turn] [policy] [max]", "Print available actions (default max: 10000).");
        return 2;
    }
    return with_position([&](const parsed_position &position) {
        state s = *position.value;
        switch(mode)
        {
            case search_mode::balanced: count_balanced<true>(s, max); break;
            case search_mode::naive: count_naive<true>(s, max); break;
            case search_mode::stable: count_stable<true>(s, max); break;
            case search_mode::iterative: count_iterative<true>(s, max); break;
            case search_mode::mixed: count_mixed<true>(s, max); break;
        }
    }, selection);
}

int run_checkmate(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "checkmate", "[--at turn] [policy]", "Determine whether the selected position is checkmate or stalemate.");
        return 0;
    }
    std::vector<const char *> positional;
    std::optional<turn_selection> selection;
    if(!parse_at_arguments(argc, argv, positional, selection)) return 2;
    if(positional.size() > 2)
    {
        print_position_help(std::cerr, "checkmate", "[--at turn] [policy]", "Determine whether the selected position is checkmate or stalemate.");
        return 2;
    }
    search_mode mode;
    int maximum;
    try { std::tie(mode, maximum) = parse_search_args(static_cast<int>(positional.size()), positional.data()); }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search policy\n";
        print_position_help(std::cerr, "checkmate", "[--at turn] [policy]", "Determine whether the selected position is checkmate or stalemate.");
        return 2;
    }
    (void)maximum;
    return with_position([&](const parsed_position &position) {
        state s = *position.value;
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
    }, selection);
}

int run_diff(int argc, const char *argv[])
{
    if(help_requested(argc, argv))
    {
        print_position_help(std::cout, "diff", "[--at turn]", "Compare balanced and naive search results.");
        return 0;
    }
    std::vector<const char *> positional;
    std::optional<turn_selection> selection;
    if(!parse_at_arguments(argc, argv, positional, selection)) return 2;
    if(positional.size() > 1)
    {
        print_position_help(std::cerr, "diff", "[--at turn]", "Compare balanced and naive search results.");
        return 2;
    }
    return with_position(
        [](const parsed_position &p) { diff(*p.value); }, selection);
}
