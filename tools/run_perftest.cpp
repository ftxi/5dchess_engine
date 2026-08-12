#include "run_perftest.h"

#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include "hypercuboid.h"
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
            << "  policy      balanced, naive, stable, iterative, or mixed\n"
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

    state current_state = state(g);
    turn_t turn = {1,false};
    while (true)
    {
        std::optional<moveseq> mvs;
        switch (mode) {
            case search_mode::balanced: {
                auto [w, ss] = HC_info::build_HC(current_state);
                mvs = w.search(ss).first();
                break;
            }
            case search_mode::naive:
                mvs = naive_search(current_state).first();
                break;
            case search_mode::stable: {
                auto [w, ss] = HC_info::build_HC(current_state);
                mvs = w.stable_search(ss).first();
                break;
            }
            case search_mode::iterative: {
                auto [w, ss] = HC_info::build_HC(current_state);
                mvs = w.iterative_search(ss).first();
                break;
            }
            case search_mode::mixed: {
                auto [w, ss] = HC_info::build_HC(current_state);
                mvs = w.mixed_search(ss).first();
                break;
            }
        }
        auto [t,c] = current_state.get_present();
        if(mvs)
        {
            std::cout << '1' << std::flush;
            if(std::holds_alternative<pgnparser_ast::gametree::variations_t>(gt->variations_or_outcome))
            {
                const auto &variations = std::get<pgnparser_ast::gametree::variations_t>(gt->variations_or_outcome);
                if(!variations.empty())
                {
                    const auto &[act, last_gt] = *(variations.end() - 1);
                    for(const auto& mv: act.moves)
                    {
                        auto [fm_opt, pt_opt, candidates] = current_state.parse_move(mv);
                        if(!fm_opt.has_value())
                        {
                            if(candidates.empty())
                            {
                                std::ostringstream oss;
                                oss << "state(): Invalid move: " << mv;
                                throw std::runtime_error(oss.str());
                            }
                            else
                            {
                                std::ostringstream oss;
                                oss << "state(): Ambiguous move: " << mv << "; candidates: ";
                                oss << range_to_string(candidates, "", "");
                                throw std::runtime_error(oss.str());
                            }
                        }
                        else
                        {
                            full_move fm = fm_opt.value();
                            bool flag;
                            if(pt_opt.has_value())
                            {
                                piece_t pt = to_white(*pt_opt);
                                flag = current_state.apply_move<false>(fm, pt);
                            }
                            else
                            {
                                flag = current_state.apply_move<false>(fm);
                            }
                            if(!flag)
                            {
                                std::ostringstream oss;
                                oss << "state(): Illegal move: " << mv << " (parsed as: " << fm << ")";
                                throw std::runtime_error(oss.str());
                            }
                        }
                    }
                    if(std::holds_alternative<pgnparser_ast::gametree::variations_t>(last_gt->variations_or_outcome))
                    {
                        const auto &last_variations = std::get<pgnparser_ast::gametree::variations_t>(last_gt->variations_or_outcome);
                        if(!last_variations.empty())
                        {
                            bool flag = current_state.submit();
                            if(!flag)
                            {
                                std::ostringstream oss;
                                oss << "state(): Cannot submit after parsing these moves: " << act;
                                throw std::runtime_error(oss.str());
                            }
                        }
                        else
                        {
                            bool flag = current_state.submit();
                            if(!flag)
                            {
                                std::cerr << "[WARNING]state(): Cannot submit after parsing these moves: " << act;
                            }
                        }
                    }
                    else
                    {
                        pgnparser_ast::token_t outcome = std::get<pgnparser_ast::token_t>(last_gt->variations_or_outcome);
                        (void)outcome;
                        // TODO: Handle game outcome token when checking continuation state.
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
                // TODO: Handle game outcome token in perftest traversal.
            }
        }
        else
        {
            std::cout << "0\n";
            if(current_state.phantom().find_checks(!c).first())
            {
                std::cout << "Turn " << show_turn(turn) << ": Checkmate";
            }
            else
            {
                std::cout << "Turn " << show_turn(turn) << ": Stalemate";
            }
            break;
        }
        turn = next_turn(turn);
    }
    std::cout << "\n";
    return 0;
}
