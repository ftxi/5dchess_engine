#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <sstream>

#include "hypercuboid.h"
#include "pgnparser.h"
#include "game.h"
#include "turn.h"

//std::string pgn1 =
//R"(
//[Size "4x4"]
//[Board "Custom"]
//[Mode "5D"]
//[nbrk/3p*/P*3/KRBN:0:1:w]
//1. Rb4 / Rxb4
//2. N>>d3 / (L1)Bc3+
//3. Nxc3
//)";

template<bool C>
generator<moveseq> naive_search_impl(state s, moveseq mvs, int k, bool b)
{
    if(s.find_checks(!C).first().has_value())
        co_return;
    if(s.can_submit())
        co_yield mvs;
    for(vec4 p : s.gen_movable_pieces())
    {
        for(vec4 q : s.gen_piece_move(p))
        {
            bool branching = std::make_pair(q.t(),C)<s.get_timeline_end(q.l());
            if(!branching && (b || (C?q.l()>k:q.l()<k)))
                continue;
            state t = *s.can_apply(full_move(p, q));
            moveseq mmvs = mvs;
            mmvs.push_back(full_move(p, q));
            for(moveseq nmvs : naive_search_impl<C>(t, mmvs, branching ? k : q.l(), branching))
                co_yield nmvs;
        }
    }
}

generator<moveseq> naive_search(state s)
{
    const auto [t,c] = s.get_present();
    const auto [lmin, lmax] = s.get_lines_range();
    return c ? naive_search_impl<true>(s, {}, lmax+1, false) : naive_search_impl<false>(s, {}, lmin-1, false);
}

//void search_all(std::string pgn)
//{
//    pgnparser_ast::game g = *pgnparser(pgn).parse_game();
//    state s(g);
//    std::cout << s.to_string() << std::endl;
//    std::cout << "starting_test:\n";
//    auto [w, ss] = HC_info::build_HC(s);
//    std::vector<moveseq> legal_moves;
////    auto it = ss.hcs.end();
////    it--; it--;
////    HC hc = *it;
////    hc.axes[0] = {0};
////    hc.axes[1] = {0};
////    std::cout << hc.to_string();
////    search_space ss1 {{hc}};
//
//    for(auto x : w.search(ss))
//    {
//        if(x.size() == 0)
//        {
//            std::cout << "No avilable action in this turn";
//        }
//        else
//        {
//            std::cout << "Valid action with " << x.size() << " moves: ";
//            legal_moves.push_back(x);
//            for(full_move m : x)
//            {
//                std::cout << s.pretty_move(m) << " ";
//            }
//        }
//        std::cout << "\n";
//        std::cout << "\n----------------------------\n\n";
//    }
//    std::cout << "\n----------------------------\n\n";
//    std::cout << "Summary: totally " << legal_moves.size() << " options\n";
//    for(moveseq x:legal_moves)
//    {
//        for(full_move m : x)
//        {
//            std::cout << s.pretty_move<>(m) << " ";
//        }
//        std::cout << "\n";
//    }
//}

template<bool PRINT=false>
void count_hc(state s, int count)
{
    auto [w, ss] = HC_info::build_HC(s);
    std::vector<moveseq> legal_moves;
    for(auto x : w.search(ss))
    {
        if constexpr(PRINT)
        {
            state t = s;
            for(full_move m : x)
            {
                std::cout << t.pretty_move<state::SHOW_CAPTURE>(m) << " ";
                t.apply_move(m);
            }
            std::cout << "\n";
        }
        legal_moves.push_back(x);
        if(--count==0)
            break;
    }
    std::cout << "Summary: totally " << legal_moves.size() << " options\n";
}

template<bool PRINT=false>
void count_naive(state s, int count)
{
    std::vector<moveseq> legal_moves;
    for(auto x : naive_search(s))
    {
        if constexpr (PRINT)
        {
            state t = s;
            for(full_move m : x)
            {
                std::cout << t.pretty_move<state::SHOW_CAPTURE>(m) << " ";
                t.apply_move(m);
            }
            std::cout << "\n";
        }
        legal_moves.push_back(x);
        if(--count==0)
            break;
    }
    std::cout << "Summary: totally " << legal_moves.size() << " options\n";
}

void diff(state s)
{
    std::set<moveseq> legal_moves_hc, legal_moves_naive;
    auto [w, ss] = HC_info::build_HC(s);
    auto start1 = std::chrono::high_resolution_clock::now();
    for(auto x : w.search(ss))
    {
        legal_moves_hc.insert(x);
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration<double, std::milli>(end1 - start1).count();
    std::cout << "computation took " << duration1 << " ms\n";
    std::cout << "hc count: " << legal_moves_hc.size() << "\n";
    auto start2 = std::chrono::high_resolution_clock::now();
    for(auto x : naive_search(s))
    {
        legal_moves_naive.insert(x);
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration<double, std::milli>(end2 - start2).count();
    std::cout << "computation took " << duration2 << " ms\n";
    std::cout << "naive count: " << legal_moves_naive.size() << "\n";
    std::set<moveseq> only1 = set_minus(legal_moves_hc, legal_moves_naive);
    std::set<moveseq> only2 = set_minus(legal_moves_naive, legal_moves_hc);
    std::cout << "\n----------------------------\n\n";
    std::cout << "only in hc (" << only1.size() << " items):\n";
    for(moveseq x:only1)
    {
        for(full_move m : x)
        {
            std::cout << s.pretty_move<state::SHOW_NOTHING>(m) << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n----------------------------\n\n";
    std::cout << "only in naive (" << only2.size() << " items):\n";
    for(moveseq x:only2)
    {
        for(full_move m : x)
        {
            std::cout << s.pretty_move<state::SHOW_CAPTURE>(m) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::endl;
}

std::string helpmsg = R"(usage: cli <option>
where <option> is one of:
  help: print this message (-h, --help)
  version: print the version (-v, --version)
  print: print the final state of the game
  count [fast|naive] [<max>]: display number of avialible moves capped by <max>
  all [fast|naive] [<max>]: display all legal moves capped by <max>
  checkmate [fast|naive]: determine whether the final state is checkmate/stalemate
  diff: compare the output of two algorithms
  perftest [fast|naive]: on each intermediate state, print 1 if it is checkmate/stalemate, 0 otherwise
default value for <max> is 10000

the game being read is input in stdin (stopped by EOF)
)";

int main(int argc, const char *argv[])
{
    if (argc <= 1 || std::string(argv[1]) == "help" || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        std::cout << helpmsg;
        return 0;
    }
    
    std::string command = argv[1];
    
    // Handle version flag
    if (command == "version" || command == "-v" || command == "--version") {
#ifdef PROJECT_VERSION_STRING
        std::cout << "5d Chess Engine version " << PROJECT_VERSION_STRING << std::endl;
#else
        std::cout << "5d Chess Engine version unknown" << std::endl;
#endif
        return 0;
    }
    
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    std::string pgn = buffer.str();
    std::unique_ptr<state> ps;
    try {
        pgnparser_ast::game g = *pgnparser(pgn).parse_game();
        ps = std::make_unique<state>(g);
    } catch (parse_error e) {
        std::cerr << "Parse Error: " << e.what() << std::endl;
        return 2;
    } catch (std::runtime_error e) {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return 1;
    }
    if (command == "print")
    {
        std::cout << ps->to_string();
    }
    else if (command == "count")
    {
        bool use_fast = true;
        int max = 10000;
        
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "fast") {
                use_fast = true;
            } else if (arg == "naive") {
                use_fast = false;
            } else {
                max = std::stoi(arg);
            }
        }
        
        if(use_fast)
        {
            count_hc(*ps, max);
        }
        else
        {
            count_naive(*ps, max);
        }
    }
    else if (command == "all")
    {
        // Parse optional arguments: [fast|naive] [<max>]
        bool use_fast = true;
        int max = 10000;
        
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "fast") {
                use_fast = true;
            } else if (arg == "naive") {
                use_fast = false;
            } else {
                max = std::stoi(arg);
            }
        }
        
        if(use_fast)
        {
            count_hc<true>(*ps, max);
        }
        else
        {
            count_naive<true>(*ps, max);
        }
    }
    else if (command == "checkmate")
    {
        bool use_fast = true;
        
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "fast") {
                use_fast = true;
            } else if (arg == "naive") {
                use_fast = false;
            }
        }
        
        std::optional<moveseq> mvs;
        if(use_fast)
        {
            auto [w, ss] = HC_info::build_HC(*ps);
            mvs = w.search(ss).first();
        }
        else
        {
            mvs = naive_search(*ps).first();
        }
        auto [t,c] =ps->get_present();
        if(mvs)
        {
            std::cout << "Not checkmate: ";
            state t = *ps;
            for(full_move m : *mvs)
            {
                std::cout << t.pretty_move<state::SHOW_CAPTURE>(m) << " ";
                t.apply_move(m);
            }
        }
        else
        {
            if(ps->phantom().find_checks(!c).first())
            {
                std::cout << "Checkmate";
            }
            else
            {
                std::cout << "Stalemate";
            }
        }
        std::cout << std::endl;
    }
    else if (command == "diff")
    {
        diff(*ps);
    }
    else if (command == "perftest")
    {
        bool use_fast = true;
        
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "fast") {
                use_fast = true;
            } else if (arg == "naive") {
                use_fast = false;
            }
        }
        pgnparser_ast::game g = *pgnparser(pgn).parse_game();
        pgnparser_ast::gametree gt_root = std::move(g.gt);
        g.gt = pgnparser_ast::gametree{{}};
        pgnparser_ast::gametree *gt = &gt_root;
        
        state current_state = state(g);
        turn_t turn = {1,false};
        while (true)
        {
            std::optional<moveseq> mvs;
            if(use_fast)
            {
                auto [w, ss] = HC_info::build_HC(current_state);
                mvs = w.search(ss).first();
            }
            else
            {
                mvs = naive_search(current_state).first();
            }
            auto [t,c] = current_state.get_present();
            if(mvs)
            {
                std::putchar('1');
                if(!gt->variations.empty())
                {
                    const auto &[act, last_gt] = *(gt->variations.end() - 1);
                    //std::cout << act;
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
                    if(!last_gt->variations.empty())
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
                    gt = last_gt.get();
                }
                else
                {
                    break;
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
    }
    else
    {
        std::cerr << "Unknown command: " << command << std::endl;
        std::cout << helpmsg;
        return 2;
    }
    return 0;
}
