#include "search_tools.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "hypercuboid.h"

template <typename T>
std::set<T> set_minus(const std::set<T>& a, const std::set<T>& b)
{
    std::set<T> result;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                        std::inserter(result, result.begin()));
    return result;
}

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

template<bool PRINT>
void count_balanced(state s, int count)
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
                std::cout << m.pgn<pgn_options::SHOW_CAPTURE>(t) << " ";
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

template<bool PRINT>
void count_stable(state s, int count)
{
    auto [w, ss] = HC_info::build_HC(s);
    std::vector<moveseq> legal_moves;
    for(auto x : w.stable_search(ss))
    {
        if constexpr(PRINT)
        {
            state t = s;
            for(full_move m : x)
            {
                std::cout << m.pgn<pgn_options::SHOW_CAPTURE>(t) << " ";
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

template<bool PRINT>
void count_iterative(state s, int count)
{
    auto [w, ss] = HC_info::build_HC(s);
    std::vector<moveseq> legal_moves;
    for(auto x : w.iterative_search(ss))
    {
        if constexpr(PRINT)
        {
            state t = s;
            for(full_move m : x)
            {
                std::cout << m.pgn<pgn_options::SHOW_CAPTURE>(t) << " ";
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

template<bool PRINT>
void count_mixed(state s, int count)
{
    auto [w, ss] = HC_info::build_HC(s);
    std::vector<moveseq> legal_moves;
    for(auto x : w.mixed_search(ss))
    {
        if constexpr(PRINT)
        {
            state t = s;
            for(full_move m : x)
            {
                std::cout << m.pgn<pgn_options::SHOW_CAPTURE>(t) << " ";
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

template<bool PRINT>
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
                std::cout << m.pgn<pgn_options::SHOW_CAPTURE>(t) << " ";
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
            std::cout << m.pgn<pgn_options::SHOW_NOTHING>(s) << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n----------------------------\n\n";
    std::cout << "only in naive (" << only2.size() << " items):\n";
    for(moveseq x:only2)
    {
        for(full_move m : x)
        {
            std::cout << m.pgn<pgn_options::SHOW_CAPTURE>(s) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::endl;
}

std::pair<search_mode, int> parse_search_args(
    int argc, const char *argv[], int start_idx)
{
    search_mode mode = search_mode::balanced;
    int max = 10000;
    for(int i = start_idx; i < argc; ++i)
    {
        std::string arg = argv[i];
        if(arg == "balanced") mode = search_mode::balanced;
        else if(arg == "naive") mode = search_mode::naive;
        else if(arg == "stable") mode = search_mode::stable;
        else if(arg == "iterative") mode = search_mode::iterative;
        else if(arg == "mixed") mode = search_mode::mixed;
        else max = std::stoi(arg);
    }
    return {mode, max};
}

std::optional<moveseq> find_first_action(state &s, search_mode mode)
{
    switch(mode)
    {
        case search_mode::balanced: {
            auto [w, ss] = HC_info::build_HC(s);
            return w.search(ss).first();
        }
        case search_mode::naive:
            return naive_search(s).first();
        case search_mode::stable: {
            auto [w, ss] = HC_info::build_HC(s);
            return w.stable_search(ss).first();
        }
        case search_mode::iterative: {
            auto [w, ss] = HC_info::build_HC(s);
            return w.iterative_search(ss).first();
        }
        case search_mode::mixed: {
            auto [w, ss] = HC_info::build_HC(s);
            return w.mixed_search(ss).first();
        }
    }
    return std::nullopt;
}

template void count_balanced<false>(state, int);
template void count_balanced<true>(state, int);
template void count_stable<false>(state, int);
template void count_stable<true>(state, int);
template void count_iterative<false>(state, int);
template void count_iterative<true>(state, int);
template void count_mixed<false>(state, int);
template void count_mixed<true>(state, int);
template void count_naive<false>(state, int);
template void count_naive<true>(state, int);
