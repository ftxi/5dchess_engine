#include "search_tools.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "hypercuboid.h"
#include "finetree.h"

namespace
{
generator<fine_node<> *> fine_ceilings(fine_node<> &node)
{
    if(node.is_ceiling())
    {
        co_yield &node;
        co_return;
    }

    // Expansion creates a whole witness branch. Visit those existing children
    // before searching for additional children at this level.
    const std::size_t existing = node.get_children().size();
    for(std::size_t j = 0; j < existing; ++j)
    {
        for(auto *ceiling : fine_ceilings(*node.get_children()[j]))
            co_yield ceiling;
    }
    for([[maybe_unused]] index_t i : node.search(natural_HC_ordering{}))
    {
        // Separate cells can produce distinct children with the same index.
        // Search appends the new branch; get_child(i) can find an older one.
        for(auto *ceiling : fine_ceilings(*node.get_children().back()))
            co_yield ceiling;
    }
}
}

namespace
{
template<bool Print, class Range, class Project = std::identity>
std::size_t count_range(const state &s, Range &&range, int limit, Project project = {})
{
    std::size_t count = 0;
    for(const auto &value : range)
    {
        if constexpr(Print)
        {
            state t = s;
            for(full_move m : project(value))
            {
                std::cout << m.pgn(t, QUEEN_W, pgn_options::SHOW_CAPTURE) << " ";
                t.apply_move<true>(m);
            }
            std::cout << "\n";
        }
        if(++count == static_cast<std::size_t>(limit)) break;
    }
    return count;
}

template<hc_policy Policy>
std::optional<moveseq> first_hc(state &s)
{
    auto [info, space] = HC_info::build_HC(s);
    return info.search<natural_HC_ordering, Policy>(std::move(space)).first();
}
} /* anonymous namespace */

template<bool PRINT>
std::size_t count_fine(state s, int limit)
{
    auto root = fine_node<>::make_root(s);
    return count_range<PRINT>(s, fine_ceilings(*root), limit,
        [](const auto *ceiling) { return ceiling->to_action(); });
}

template <typename T>
std::set<T> set_minus(const std::set<T> &a, const std::set<T> &b)
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
    if(s.test_present())
        co_yield mvs;
    for(vec4 p : s.get_movable_pieces())
    {
        for(vec4 q : s.gen_piece_move(p))
        {
            bool branching = turn(q.t(),C)<s.get_timeline_end(q.l());
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
    const bool c = s.get_present().c();
    const auto [lmin, lmax] = s.get_lines_range();
    return c ? naive_search_impl<true>(s, {}, lmax+1, false) : naive_search_impl<false>(s, {}, lmin-1, false);
}

template<hc_policy Policy, bool Print>
std::size_t count_hc(state s, int limit)
{
    auto [info, space] = HC_info::build_HC(s);
    return count_range<Print>(s,
        info.search<natural_HC_ordering, Policy>(std::move(space)), limit);
}

template<bool PRINT>
std::size_t count_naive(state s, int limit)
{
    return count_range<PRINT>(s, naive_search(s), limit);
}

void diff(state s)
{
    std::set<moveseq> legal_moves_hc, legal_moves_naive;
    auto [w, ss] = HC_info::build_HC(s);
    auto start1 = std::chrono::high_resolution_clock::now();
    for(auto x : w.search<natural_HC_ordering, hc_policy::balanced>(std::move(ss)))
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
            std::cout << m.pgn(s, QUEEN_W, pgn_options::SHOW_NOTHING) << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n----------------------------\n\n";
    std::cout << "only in naive (" << only2.size() << " items):\n";
    for(moveseq x:only2)
    {
        for(full_move m : x)
        {
            std::cout << m.pgn(s, QUEEN_W, pgn_options::SHOW_CAPTURE) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::endl;
}

std::pair<search_mode, int> parse_search_args(
    int argc, const char *argv[], int start_idx)
{
    search_mode mode = search_mode::deferred;
    int max = 10000;
    for(int i = start_idx; i < argc; ++i)
    {
        std::string arg = argv[i];
        if(arg == "balanced") mode = search_mode::balanced;
        else if(arg == "naive") mode = search_mode::naive;
        else if(arg == "stable") mode = search_mode::stable;
        else if(arg == "deferred") mode = search_mode::deferred;
        else if(arg == "iterative") mode = search_mode::iterative;
        else if(arg == "fine") mode = search_mode::fine;
        else max = std::stoi(arg);
    }
    return {mode, max};
}

std::optional<moveseq> find_first_action(state &s, search_mode mode)
{
    switch(mode)
    {
        case search_mode::balanced: return first_hc<hc_policy::balanced>(s);
        case search_mode::stable: return first_hc<hc_policy::stable>(s);
        case search_mode::deferred: return first_hc<hc_policy::deferred>(s);
        case search_mode::iterative: return first_hc<hc_policy::iterative>(s);
        case search_mode::naive: return naive_search(s).first();
        case search_mode::fine: {
            auto root = fine_node<>::make_root(s);
            if(root->is_terminal()) return std::nullopt;
            return finetree_nearby_ceiling(root.get())->to_action();
        }
    }
    return std::nullopt;
}

std::size_t count_actions(state s, search_mode mode, int limit)
{
    if(limit <= 0) throw std::invalid_argument("action limit must be positive");
    switch(mode)
    {
        case search_mode::balanced: return count_hc<hc_policy::balanced>(std::move(s), limit);
        case search_mode::naive: return count_naive(std::move(s), limit);
        case search_mode::stable: return count_hc<hc_policy::stable>(std::move(s), limit);
        case search_mode::deferred: return count_hc<hc_policy::deferred>(std::move(s), limit);
        case search_mode::iterative: return count_hc<hc_policy::iterative>(std::move(s), limit);
        case search_mode::fine: return count_fine(std::move(s), limit);
    }
    throw std::invalid_argument("unknown search policy");
}

template std::size_t count_hc<hc_policy::balanced, false>(state, int);
template std::size_t count_hc<hc_policy::balanced, true>(state, int);
template std::size_t count_hc<hc_policy::stable, false>(state, int);
template std::size_t count_hc<hc_policy::stable, true>(state, int);
template std::size_t count_hc<hc_policy::deferred, false>(state, int);
template std::size_t count_hc<hc_policy::deferred, true>(state, int);
template std::size_t count_hc<hc_policy::iterative, false>(state, int);
template std::size_t count_hc<hc_policy::iterative, true>(state, int);
template std::size_t count_naive<false>(state, int);
template std::size_t count_naive<true>(state, int);
template std::size_t count_fine<false>(state, int);
template std::size_t count_fine<true>(state, int);
