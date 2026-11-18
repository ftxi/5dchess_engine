#include "run_perftest.h"

#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include "pgnparser.h"
#include "search_tools.h"
#include "turn.h"
#include "utils.h"

int run_perftest(int argc, const char *argv[])
{
    const auto print_help = [](std::ostream &out) {
        out << "Usage: 5dtools perftest [policy]\n"
            << "  On each intermediate position, print 1 if actions are available and 0 otherwise.\n"
            << "  Reads a 5DPGN game from stdin.\n"
            << "  policy      deferred (default), balanced, naive, stable, iterative, or fine\n"
            << "  -h, --help  display this help text and exit\n";
    };
    if(argc == 2 && (std::string_view(argv[1]) == "-h"
                     || std::string_view(argv[1]) == "--help"))
    {
        print_help(std::cout);
        return 0;
    }
    if(argc > 2)
    {
        std::cerr << "Error: too many arguments\n";
        print_help(std::cerr);
        return 2;
    }

    search_mode mode;
    int ignored_maximum;
    try
    {
        std::tie(mode, ignored_maximum) = parse_search_args(argc, argv);
    }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid search policy: " << argv[1] << '\n';
        print_help(std::cerr);
        return 2;
    }
    (void)ignored_maximum;

    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    std::string pgn = buffer.str();
    pgnparser_ast::game g = *pgnparser(pgn).parse_game();
    pgnparser_ast::gametree gt_root = std::move(g.gt);
    g.gt = pgnparser_ast::gametree{};
    pgnparser_ast::gametree *gt = &gt_root;

    state current_state = state::from_ast(g);
    turn turn = {1,false};
    while (true)
    {
        const auto mvs = find_first_action(current_state, mode);
        if(mvs)
        {
            std::cout << '1' << std::flush;
            if(std::holds_alternative<pgnparser_ast::gametree::variations_t>(gt->variations_or_outcome))
            {
                const auto &variations = std::get<pgnparser_ast::gametree::variations_t>(gt->variations_or_outcome);
                if(!variations.empty())
                {
                    const auto &[act, last_gt] = *(variations.end() - 1);
                    for(const auto &mv: act.moves)
                        current_state.apply_move_ast(mv, g.source_text);
                    const auto submit_action = [&] {
                        if(!current_state.test_present()) return false;
                        current_state.submit<true>();
                        if(auto exposure = current_state.find_royal_exposure(false))
                            throw pgn_validation_error(*exposure,
                                pgn_validation_error::make_subject(act, g.source_text));
                        return true;
                    };
                    if(std::holds_alternative<pgnparser_ast::gametree::variations_t>(last_gt->variations_or_outcome))
                    {
                        const auto &last_variations = std::get<pgnparser_ast::gametree::variations_t>(last_gt->variations_or_outcome);
                        if(!last_variations.empty())
                        {
                            bool flag = submit_action();
                            if(!flag)
                            {
                                throw pgn_validation_error(pgn_validation_error::present_not_advanced{},
                                    pgn_validation_error::make_subject(act, g.source_text));
                            }
                        }
                        else
                        {
                            bool flag = submit_action();
                            if(!flag)
                            {
                                const pgn_validation_error error(pgn_validation_error::present_not_advanced{},
                                    pgn_validation_error::make_subject(act, g.source_text));
                                std::cerr << "[WARNING] " << error.what();
                            }
                        }
                    }
                    else
                    {
                        pgnparser_ast::token_t outcome = std::get<pgnparser_ast::token_t>(last_gt->variations_or_outcome);
                        (void)outcome;
                        bool flag = submit_action();
                        if(!flag)
                        {
                            throw pgn_validation_error(pgn_validation_error::present_not_advanced{},
                                pgn_validation_error::make_subject(act, g.source_text));
                        }
                    }
                    gt = last_gt.get();
                }
                else
                {
                    break;
                }
            }
            else
            {
                pgnparser_ast::token_t outcome = std::get<pgnparser_ast::token_t>(gt->variations_or_outcome);
                (void)outcome;
                break;
            }
        }
        else
        {
            std::cout << "0\n";
            if(current_state.has_phantom_check())
            {
                std::cout << "Turn " << turn.to_string() << ": Checkmate";
            }
            else
            {
                std::cout << "Turn " << turn.to_string() << ": Stalemate";
            }
            break;
        }
        turn = turn.next_turn();
    }
    std::cout << "\n";
    return 0;
}
