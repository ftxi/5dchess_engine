#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <set>
#include <vector>
#include "hypercuboid.h"

// An independent small-space oracle: enumerate matching points, remove only
// individual points, and validate them. No Deferred conflict propagation.
std::vector<point> legal_points(const HC_info &info, search_space space)
{
    std::vector<point> points;
    while(!space.empty())
    {
        HC hc = std::move(space.back());
        space.pop_back();
        auto p = info.take_point(hc);
        if(!p) continue;
        if(!info.find_problem(*p, info.get_universe())) points.push_back(*p);
        space.concat(std::move(hc).remove_point(*p));
    }
    return points;
}

void check_position(const state &position)
{
    auto [info, initial] = HC_info::build_HC(position);
    const auto expected = legal_points(info, initial);
    HC_info::witness_space<hc_policy::deferred> linear(initial);
    HC_info::witness_space<hc_policy::tree> tree(initial);
    std::vector<HC> queries{info.get_universe()};
    // Fixed prefix schedule, constructed from domains before either solver runs.
    // Include all one/two-coordinate prefixes, then interleave their order.
    for(index_t i : info.get_universe()[0])
    {
        HC prefix = info.get_universe();
        prefix[0] = integer_set{i};
        queries.push_back(prefix);
        if(info.dimension() > 1)
            for(index_t j : info.get_universe()[1])
            {
                HC deeper = prefix;
                deeper[1] = integer_set{j};
                queries.push_back(std::move(deeper));
            }
        // A shrinking next-coordinate domain, independent of discovered witnesses.
        HC shrinking = prefix;
        if(info.dimension() > 1)
            for(index_t j : info.get_universe()[1])
            {
                shrinking[1].erase(j);
                queries.push_back(shrinking);
            }
    }
    std::mt19937 rng(42);
    std::shuffle(queries.begin(), queries.end(), rng);
    for(int pass = 0; pass < 2; ++pass)
        for(const HC &query : queries)
        {
            const bool exists = std::any_of(expected.begin(), expected.end(),
                [&](const point &p) { return query.contains(p); });
            const auto a = info.find_witness(linear, query);
            const auto b = info.find_witness(tree, query);
            assert(a.has_value() == exists && b.has_value() == exists);
            for(const auto *answer : {&a, &b})
                if(*answer)
                {
                    assert(query.contains(**answer));
                    assert(std::find(expected.begin(), expected.end(), **answer) != expected.end());
                }
        }
    // Exhausting narrow queries must not consume the shared universe.
    assert(info.find_witness(tree, info.get_universe()).has_value() == !expected.empty());

    const auto compare = [&](const auto &order) {
        auto a = info.search<std::decay_t<decltype(order)>, hc_policy::deferred>(initial, order);
        auto b = info.search<std::decay_t<decltype(order)>, hc_policy::tree>(initial, order);
        std::set<moveseq> actions;
        for(;;)
        {
            auto left = a.first(), right = b.first();
            assert(left == right); // Same leaf traversal and split decisions.
            if(!left) break;
            assert(actions.insert(*right).second);
            assert(position.can_apply(action::from_moveseq(*right, position)));
        }
        std::set<moveseq> oracle;
        for(const auto &p : expected) oracle.insert(info.to_action(p));
        assert(actions == oracle);
    };
    compare(natural_HC_ordering{});
    compare(random_HC_ordering(info.get_universe(), rng));
}

int main()
{
    for(int turn : {3, 5})
        check_position(state::from_pgn(
            "[Board \"custom\"]\n[Size \"4x4\"]\n"
            "[3k/4/4/KN2:-2:" + std::to_string(turn) + ":b]\n"
            "[3k/4/4/KN2:-1:5:w]\n[3k/4/4/KN2:-1:5:b]\n"
            "[3k/4/4/KN2:-1:6:w]\n[3k/4/4/KN2:0:5:w]\n"));
    for(const char *board : {"3k/4/4/KN2", "3k/4/1r2/KN2"})
        check_position(state::from_pgn(std::string("[Board \"custom\"]\n[Size \"4x4\"]\n[")
            + board + ":0:1:w]\n[3k/4/4/KN2:1:1:w]\n"));
}
