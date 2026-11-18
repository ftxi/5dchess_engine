#undef NDEBUG
#include <iostream>
#include <cassert>
#include "hypercuboid.h"
#include "pgnparser.h"
#include "graph.h"
#include <cstdlib>
#include <optional>
#include <numeric>
#include <random>
#include <utility>
#include <type_traits>
#include <limits>
#include <vector>
#include <set>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

namespace hcutils_tests
{
static_assert(std::is_nothrow_move_constructible_v<HC_info>);
static_assert(std::is_nothrow_move_assignable_v<HC_info>);
static_assert(std::is_copy_constructible_v<HC_info>);
static_assert(std::is_copy_assignable_v<HC_info>);

void test_hc()
{
    HC hc {{{1,2}, {2,3}, {1,2,3}}};
    std::cout << hc.to_string() << "\n";
    point a {1,2,3};
    std::cout << hc.contains(a) << "\n";
    search_space ss1 = hc.remove_point(a);
    std::cout << ss1.to_string() << "\n";
    
    std::map<index_t, integer_set> fixed_axes = {{0,{1}},{2,{1,2}}};
    slice sl {fixed_axes};
    search_space ss2 = hc.remove_slice(sl);
    std::cout << ss2.to_string() << "\n";
}

void test_remove()
{
    HC hc {integer_set{0}, integer_set{2}};
    std::cout << hc.to_string() << "\n";
    std::map<index_t, integer_set> fixed_axes = {{0,{0}}};
    slice sl {fixed_axes};
    search_space ss2 = hc.remove_slice(sl);
    std::cout << ss2.to_string() << "\n";
}

void test_remove_point_partition()
{
    // Exhaust every point in mixed singleton/non-singleton products, including
    // coordinates spanning several integer_set blocks and the zero-axis case.
    for(std::size_t dimension = 0; dimension <= 4; ++dimension)
    {
        for(unsigned mask = 0; mask < (1u << dimension); ++mask)
        {
            std::vector<integer_set> axes;
            std::size_t expected_pieces = 0;
            for(index_t axis = 0; axis < dimension; ++axis)
            {
                const bool multiple = (mask & (1u << axis)) != 0;
                const auto low = static_cast<index_t>(axis + 1);
                const auto middle = static_cast<index_t>(axis + 65);
                const auto high = static_cast<index_t>(axis + 129);
                axes.push_back(multiple ? integer_set{low, middle, high} : integer_set{middle});
                expected_pieces += multiple;
            }
            const HC hc(std::move(axes));
            const auto before = hc.to_string();
            std::vector<point> points;
            point current(dimension);
            const auto enumerate = [&](auto &&self, std::size_t axis) -> void {
                if(axis == dimension)
                {
                    points.push_back(current);
                    return;
                }
                for(index_t coordinate : hc[axis])
                {
                    current[axis] = coordinate;
                    self(self, axis + 1);
                }
            };
            enumerate(enumerate, 0);
            for(const point &removed : points)
            {
                const auto pieces = hc.remove_point(removed);
                HC owned = hc;
                const auto consumed = std::move(owned).remove_point(removed);
                assert(consumed.size() == pieces.size());
                auto expected_piece = pieces.begin();
                for(const HC &actual_piece : consumed)
                {
                    assert(expected_piece != pieces.end());
                    assert(actual_piece.to_string() == expected_piece->to_string());
                    ++expected_piece;
                }
                assert(expected_piece == pieces.end());
                assert(pieces.size() == expected_pieces);
                assert(hc.to_string() == before);
                auto piece = pieces.begin();
                // Output order is increasing split axis. Earlier axes are
                // fixed to the point; later axes retain their original sets.
                for(std::size_t split = 0; split < dimension; ++split)
                {
                    if(hc[split].size() == 1) continue;
                    assert(piece != pieces.end() && !piece->empty());
                    for(std::size_t axis = 0; axis < dimension; ++axis)
                    {
                        integer_set expected = axis < split ? integer_set{removed[axis]} : hc[axis];
                        if(axis == split) expected.erase(removed[axis]);
                        assert((*piece)[axis].is_subset_of(expected));
                        assert(expected.is_subset_of((*piece)[axis]));
                    }
                    ++piece;
                }
                assert(piece == pieces.end());
                for(const point &candidate : points)
                {
                    std::size_t containing = 0;
                    for(const HC &region : pieces) containing += region.contains(candidate);
                    assert(containing == (candidate == removed ? 0 : 1));
                }
            }
        }
    }
}

void test_consuming_slice_partition()
{
    const auto compare = [](const search_space &actual, const search_space &expected) {
        assert(actual.size() == expected.size());
        auto reference = expected.begin();
        for(const HC &piece : actual)
        {
            assert(reference != expected.end());
            assert(piece.dimension() == reference->dimension());
            for(index_t axis = 0; axis < piece.dimension(); ++axis)
            {
                assert(piece[axis].is_subset_of((*reference)[axis]));
                assert((*reference)[axis].is_subset_of(piece[axis]));
            }
            ++reference;
        }
        assert(reference == expected.end());
    };
    for(std::size_t dimension = 0; dimension <= 3; ++dimension)
    {
        for(unsigned shape = 0; shape < (1u << dimension); ++shape)
        {
            std::vector<integer_set> axes;
            for(index_t axis = 0; axis < dimension; ++axis)
            {
                const auto middle = static_cast<index_t>(axis + 65);
                axes.push_back(shape & (1u << axis)
                    ? integer_set{static_cast<index_t>(axis + 1), middle,
                                  static_cast<index_t>(axis + 129)}
                    : integer_set{middle});
            }
            const HC hc(std::move(axes));
            const auto before = hc.to_string();
            unsigned combinations = 1;
            for(std::size_t axis = 0; axis < dimension; ++axis) combinations *= 8;
            for(unsigned pattern = 0; pattern < combinations; ++pattern)
            {
                slice cut;
                unsigned options = pattern;
                bool plain_valid = true;
                for(index_t axis = 0; axis < dimension; ++axis, options /= 8)
                {
                    const auto choice = options % 8;
                    if(choice == 0) continue; // free axis
                    const auto middle = static_cast<index_t>(axis + 65);
                    integer_set values;
                    if(choice == 2) values.insert(250); // disjoint
                    if(choice == 3 || choice == 4 || choice == 7) values.insert(middle);
                    if(choice == 4) values.insert(static_cast<index_t>(axis + 1));
                    if(choice == 5 || choice == 6) values = hc[axis];
                    if(choice == 6 || choice == 7) values.insert(250);
                    plain_valid &= !values.empty() && values.is_subset_of(hc[axis]);
                    cut.fix_axis(axis, std::move(values));
                }
                const auto expected = hc.remove_slice_carefully(cut);
                HC owned = hc;
                const auto actual = std::move(owned).remove_slice_carefully(cut);
                compare(actual, expected);
                if(plain_valid)
                {
                    owned = hc;
                    compare(std::move(owned).remove_slice(cut), hc.remove_slice(cut));
                }
                for(index_t limit = 0; limit <= dimension; ++limit)
                {
                    owned = hc;
                    compare(std::move(owned).remove_slice_if_good(cut, limit),
                            hc.remove_slice_if_good(cut, limit));
                }
                point candidate(dimension);
                const auto coverage = [&](auto &&self, std::size_t axis) -> void {
                    if(axis == dimension)
                    {
                        std::size_t count = 0;
                        for(const HC &piece : actual) count += piece.contains(candidate);
                        assert(count == (cut.contains(candidate) ? 0u : 1u));
                        return;
                    }
                    for(index_t coordinate : hc[axis])
                    {
                        candidate[axis] = coordinate;
                        self(self, axis + 1);
                    }
                };
                coverage(coverage, 0);
                assert(hc.to_string() == before);
            }
        }
    }
    // Empty HCs retain the borrowing implementation's exact piece behavior.
    const HC empty{{}, {1, 65}};
    for(const slice &cut : {slice{}, slice{{{0, {1}}}}, slice{{{1, {1}}}}})
    {
        HC owned = empty;
        compare(std::move(owned).remove_slice_carefully(cut), empty.remove_slice_carefully(cut));
    }
}

void test_backward_slice_removal()
{
    const slice problem{{{0, {0}}}};

    // The back HC intersects, the preceding HC does not, so the dynamic
    // density rule stops before reaching the intersecting front HC.
    search_space local{
        HC{{0, 1}},
        HC{{2}},
        HC{{0, 3}}
    };
    local.remove_slice_backwards(problem, false);
    assert(local.size() == 3);
    assert(local.contains(point{0}));
    assert(local.contains(point{2}));
    assert(local.contains(point{3}));

    // With the fixed weight of 2, three hits provide enough evidence to cross
    // one miss and reach the intersecting front HC.
    search_space dense_enough{
        HC{{0, 4}},
        HC{{5}},
        HC{{0, 6}},
        HC{{0, 3}},
        HC{{0, 2}}
    };
    dense_enough.remove_slice_backwards(problem, false);
    assert(!dense_enough.contains(point{0}));
    assert(dense_enough.contains(point{2}));
    assert(dense_enough.contains(point{3}));
    assert(dense_enough.contains(point{4}));
    assert(dense_enough.contains(point{5}));
    assert(dense_enough.contains(point{6}));

    // HC search uses a stricter weight of 10, so the same miss stops
    // propagation before the front HC is reached.
    search_space sparse_for_search{
        HC{{0, 4}}, HC{{5}}, HC{{0, 6}}, HC{{0, 3}}, HC{{0, 2}}
    };
    sparse_for_search.remove_slice_backwards(problem, false, 10);
    assert(sparse_for_search.contains(point{0}));
    assert(sparse_for_search.back().contains(point{2}));

    // A slice may only partially intersect an HC on several axes. Backward
    // removal must not introduce slice coordinates that were absent from the
    // original HC.
    search_space rejected_as_too_fragmented{
        HC{{0, 1}, {0, 1}}
    };
    const slice broad_problem{{
        {0, {0, 9}},
        {1, {0, 9}}
    }};
    assert(!rejected_as_too_fragmented.back()
                .is_slice_good(broad_problem));
    assert(rejected_as_too_fragmented.back()
               .is_slice_good(broad_problem, 2));
    rejected_as_too_fragmented.remove_slice_backwards(
        broad_problem, false);
    assert(rejected_as_too_fragmented.size() == 1);
    assert(rejected_as_too_fragmented.contains(point{0, 0}));

    // Raising the limit permits this two-axis cut without introducing
    // coordinates outside the original HC.
    rejected_as_too_fragmented.remove_slice_backwards(
        broad_problem, false, 2, 2);
    assert(!rejected_as_too_fragmented.contains(point{0, 0}));
    assert(rejected_as_too_fragmented.volume() == 3);
    assert(rejected_as_too_fragmented.contains(point{0, 1}));
    assert(rejected_as_too_fragmented.contains(point{1, 0}));
    assert(rejected_as_too_fragmented.contains(point{1, 1}));

    // The originating HC must be cut even when the same operation would be
    // rejected for a secondary HC, or exploration could repeat forever.
    search_space partial{
        HC{{0, 1}, {0, 1}}
    };
    partial.remove_slice_backwards(broad_problem, true);
    assert(!partial.contains(point{0, 0}));
    assert(partial.contains(point{0, 1}));
    assert(partial.contains(point{1, 0}));
    assert(partial.contains(point{1, 1}));
    assert(!partial.contains(point{9, 1}));
    assert(!partial.contains(point{1, 9}));

    // With no intervening miss, all intersecting HCs are pruned.
    search_space dense{
        HC{{0, 1}},
        HC{{0, 2}}
    };
    dense.remove_slice_backwards(problem, false);
    assert(dense.size() == 2);
    assert(!dense.contains(point{0}));
    assert(dense.contains(point{1}));
    assert(dense.contains(point{2}));

}

void test_slice_good()
{
    static_assert(noexcept(std::declval<const HC &>().is_slice_good(
        std::declval<const slice &>())));
    const HC hc{{1, 64, 4096}, {2, 65}};
    const slice whole{{{0, {1, 64, 4096, 8192}}, {1, {2, 65}}}};
    const slice partial{{{0, {64}}, {1, {2}}}};
    const slice disjoint{{{0, {128}}}};
    assert(hc.is_slice_good(whole, 0));
    assert(!hc.is_slice_good(partial, 0));
    assert(!hc.is_slice_good(partial, 1));
    assert(hc.is_slice_good(partial, 2));
    assert(!hc.is_slice_good(disjoint, 2));
    assert(hc.is_slice_good(slice{}, 0));
    assert(hc.volume() == 6);
}

void test_backward_slice_order()
{
    const slice problem{{{0, {0}}, {1, {0}}}};
    const HC h1{{9}, {9}};
    const HC h2{{0, 2}, {0, 2}};
    const HC h3{{3}, {3}};
    const HC h4{{0, 4}, {0, 4}};
    search_space space{h1, h2, h3, h4};
    // Scan across h3 to cut h2 as well. The pieces must retain both their
    // own order and their original HC's position in the list. A limit of
    // two permits both h2 and h4 to produce two pieces.
    space.remove_slice_backwards(problem, true, 0, 2);
    search_space expected{h1};
    expected.concat(h2.remove_slice(problem));
    expected.push_back(h3);
    expected.concat(h4.remove_slice(problem));
    assert(space.to_string() == expected.to_string());
}

void test_global_slice_order()
{
    const slice problem{{{0, {0, 99}}, {1, {0, 99}}}};
    const HC a{{9}, {9}};
    const HC b{{0, 2}, {0, 2}};
    const HC c{{3}, {3}};
    const HC removed{{0}, {0}};
    const HC e{{0, 4}, {0, 4}};
    const HC f{{5}, {5}};
    search_space space{a, b, c, removed, e, f};
    search_space expected{a};
    expected.concat(b.remove_slice_carefully(problem));
    expected.push_back(c);
    expected.concat(e.remove_slice_carefully(problem));
    expected.push_back(f);
    space.remove_slice_globally(problem);
    assert(space.to_string() == expected.to_string());
    assert(space.volume() == 9);
    assert(!space.contains(point{99, 2}));
    // A completed cut is idempotent, and cancellation before removal changes nothing.
    space.remove_slice_globally(problem);
    assert(space.to_string() == expected.to_string());
    std::stop_source stopped;
    stopped.request_stop();
    space.remove_slice_globally(slice{}, stopped.get_token());
    assert(space.to_string() == expected.to_string());
    space.remove_slice_globally(slice{});
    assert(space.empty());
    space.remove_slice_globally(problem);
}

struct counted_pass_order
{
    std::size_t *pass_hcs;
    const integer_set &operator()(index_t axis, const integer_set &coords) const
    {
        if(axis == 0 && coords.contains(0)) ++*pass_hcs;
        return coords;
    }
};

void test_balanced_stable_transition(const HC_info &info, search_space initial)
{
    // Each bad HC contains the same matching point that fails Present. A
    // disjoint, empty sentinel stops adaptive propagation between bad HCs.
    // Global propagation removes all bad HCs after discovering one problem.
    HC candidate = initial.back();
    auto pt = info.take_point(candidate);
    assert(pt && (*pt)[0] == 0 && info.test_present(*pt, info.get_universe()));
    std::vector<integer_set> axes;
    for(index_t coordinate : *pt) axes.push_back(integer_set{coordinate});
    const HC bad(std::move(axes));
    HC sentinel = bad;
    sentinel[0] = integer_set{1};
    sentinel[1] = integer_set{};
    search_space space;
    for(int i = 0; i < 113; ++i)
    {
        space.push_back(sentinel);
        space.push_back(bad);
    }
    std::size_t stable_pass_hcs = 0;
    assert((!info.search<counted_pass_order, hc_policy::stable>(space, counted_pass_order{&stable_pass_hcs}).first()));
    assert(stable_pass_hcs == 1);
    std::size_t balanced_pass_hcs = 0;
    assert((!info.search<counted_pass_order, hc_policy::balanced>(space, counted_pass_order{&balanced_pass_hcs}).first()));
    // Exactly 100 local/adaptive problems, then one stable problem. The custom
    // ordering is still called after the transition, rather than reset.
    assert(balanced_pass_hcs == 101);
    std::size_t explicit_deferred = 0, default_deferred = 0;
    assert((!info.search<counted_pass_order, hc_policy::deferred>(
        space, counted_pass_order{&explicit_deferred}).first()));
    assert(!info.search(space, counted_pass_order{&default_deferred}).first());
    assert(explicit_deferred == default_deferred);
}

void test_search_branch_order()
{
    auto parsed = pgnparser(R"(
[Board "Standard - Turn Zero"]
1. e3 / e6
2. Nf3 / Nc6
)").parse_game();
    assert(parsed);
    const state s = state::from_ast<true>(*parsed);
    for(bool stable : {false, true})
    {
        auto [info, space] = HC_info::build_HC(s);
        assert(info.dimension() == info.get_universe().dimension());
        const auto [mandatory, optional, unplayable] = s.get_timeline_status();
        index_t axis = 0;
        for(const auto &lines : {mandatory, optional})
            for(int line : lines)
                assert(info.line_to_axis(line) == axis++);
        assert(axis == info.first_branch_axis());
        const int first_line = s.new_line();
        for(; axis < info.dimension(); ++axis)
            assert(info.line_to_axis(first_line + signum(first_line)
                       * (axis - info.first_branch_axis())) == axis);
        assert(!info.line_to_axis(std::numeric_limits<int>::max()));
        assert(info.get_state().get_boards() == s.get_boards());

        // Moving a completed context before creating its generator preserves search.
        HC_info moved(std::move(info));
        auto actions = stable ? moved.search<natural_HC_ordering, hc_policy::stable>(std::move(space))
                              : moved.search<natural_HC_ordering, hc_policy::balanced>(std::move(space));
        int count = 0;
        for(const auto &moves : actions)
        {
            std::vector<ext_move> extended;
            for(const auto &move : moves)
            {
                extended.emplace_back(move, s);
            }
            const action act = action::from_vector(extended, s);
            assert(act.get_branching_index() == static_cast<int>(moves.size()));
            assert(s.can_apply(act));
            if(++count == 10) break;
        }
        assert(count == 10);
    }
}

void test_line_axis_lookup()
{
    for(const auto layout : {timeline_layout::odd, timeline_layout::even})
    {
        for(bool player : {false, true})
        {
            std::vector<boards_info_t> boards;
            for(int l = -2; l <= 2; ++l)
                boards.emplace_back(l, l == -1 ? 4 : 3,
                    l == -1 ? !player : player, "2nk/4/4/KN2");
            const state s(multiverse(layout, boards, 4, 4));
            assert(s.get_present().c() == player);
            auto [info, space] = HC_info::build_HC(s);
            const auto [mandatory, optional, unplayable] = s.get_timeline_status();
            index_t axis = 0;
            for(const auto &lines : {mandatory, optional})
                for(int line : lines)
                    assert(info.line_to_axis(line) == axis++);
            assert(axis == info.first_branch_axis());
            assert(!unplayable.empty());
            for(int line : unplayable) assert(!info.line_to_axis(line));

            const int branch_count = static_cast<int>(info.dimension() - axis);
            assert(branch_count > 0);
            const int first_branch = s.new_line();
            for(int i = 0; i < branch_count; ++i)
                assert(info.line_to_axis(first_branch + (player ? -i : i)) == axis++);
            const int lo = -2 - (player ? branch_count : 0);
            const int hi = 2 + (player ? 0 : branch_count);
            assert(!info.line_to_axis(lo - 1));
            assert(!info.line_to_axis(hi + 1));
            assert(!info.line_to_axis(std::numeric_limits<int>::min()));
            assert(!info.line_to_axis(std::numeric_limits<int>::max()));
        }
    }
}

void test_point_workspace()
{
    HC_info::workspace ws;
    // Alternate dimensions and revisit contexts after failures. Reuse must
    // preserve both the selected point and unsupported-arrival pruning.
    for(const auto *pgn : {
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n",
        "[Board \"custom\"]\n[Size \"4x4\"]\n[3k/4/4/KN2:0:1:w]\n",
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n"})
    {
        auto [info, space] = HC_info::build_HC(state::from_pgn(pgn));
        for(const HC &original : space)
        {
            for(unsigned repeat = 0; repeat < 2; ++repeat)
            {
                HC fresh = original, reused = original;
                const auto expected = info.take_point(fresh);
                assert(info.take_point(reused, natural_HC_ordering{}, &ws) == expected);
                assert(reused.to_string() == fresh.to_string());
                if(info.dimension() != 0)
                {
                    HC empty = original;
                    empty[0].clear();
                    assert(!info.take_point(empty, natural_HC_ordering{}, &ws));
                }
            }
        }
    }

    // Reset adjacency rows across bit-set blocks and graph size changes.
    graph g(130);
    graph::workspace matching_ws;
    g.add_edge(0, 129);
    assert(g.find_matching({0, 129}, matching_ws));
    for(index_t n : {index_t{130}, index_t{3}, index_t{130}})
    {
        g.reset(n);
        assert(!g.find_matching({0}, matching_ws));
        g.add_edge(0, 1);
        assert(g.find_matching({0, 1}, matching_ws));
    }
    g.reset(0);
    assert(g.find_matching({}, matching_ws));
}

void test_negative_point_selection()
{
    const auto position = state::from_pgn(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n");
    auto [info, space] = HC_info::build_HC(position);
    HC_info::workspace ws;
    for(const HC &original : space)
    {
        HC hc = original;
        auto first = info.take_point(hc);
        if(!first) continue;
        const auto before = hc.to_string();
        std::vector<slice> negative;
        // Excluding one matching witness must still find alternatives, even
        // when temporary regions contain unsupported arrivals or no matching.
        slice excluded;
        for(index_t axis = 0; axis < hc.dimension(); ++axis)
            excluded.fix_axis(axis, {(*first)[axis]});
        negative.push_back(std::move(excluded));
        for(unsigned step = 0; step < 4; ++step)
        {
            auto reused_hc = hc;
            auto actual = info.take_point(hc, natural_HC_ordering{}, nullptr, negative);
            auto reused = info.take_point(reused_hc, natural_HC_ordering{}, &ws, negative);
            assert(reused.has_value() == actual.has_value());
            if(reused)
            {
                assert(hc.contains(*reused));
                for(const slice &problem : negative) assert(!problem.contains(*reused));
            }
            assert(reused_hc.to_string() == hc.to_string());
            assert(hc.to_string() == before);
            // A retry's seed must not affect the next direct selection.
            HC direct = hc;
            assert(info.take_point(direct, natural_HC_ordering{}, &ws) == first);
            auto remaining = search_space{hc};
            for(const slice &problem : negative) remaining.remove_slice_globally(problem);
            bool exists = false;
            for(HC &region : remaining)
                if(info.take_point(region)) { exists = true; break; }
            assert(actual.has_value() == exists);
            if(!actual) break;
            assert(hc.contains(*actual));
            for(const slice &problem : negative) assert(!problem.contains(*actual));
            slice next;
            next.fix_axis(0, {(*actual)[0]});
            negative.push_back(std::move(next));
        }
        // Several negative slices can jointly cover an HC without any one
        // slice covering it. Exhaustion must account for their union.
        negative.clear();
        for(index_t coordinate : hc[0])
            negative.emplace_back(std::map<index_t, integer_set>{{0, {coordinate}}});
        assert(!info.take_point(hc, natural_HC_ordering{}, nullptr, negative));
        assert(hc.to_string() == before);
        negative = {slice{}};
        assert(!info.take_point(hc, natural_HC_ordering{}, nullptr, negative));
    }
}

struct paired_projection_order : detail::precomputed_HC_ordering<paired_projection_order>
{
    paired_projection_order(const HC &universe, index_t target, index_t preferred, index_t rejected)
    {
        for(index_t axis = 0; axis < universe.dimension(); ++axis)
        {
            auto &values = orderings.emplace_back();
            if(axis == target)
            {
                values.push_back(preferred);
                values.push_back(rejected);
            }
            for(index_t i : universe[axis])
                if(axis != target || (i != preferred && i != rejected)) values.push_back(i);
        }
    }
};

void test_paired_similar_rectangle()
{
    for(const char *destination : {"3k/4/4/KN2", "3k/4/1r2/KN2", "3k/4/4/rN1K"})
    {
        for(bool reverse : {false, true})
        {
            const int source_line = reverse ? 1 : 0;
            const int destination_line = reverse ? 0 : 1;
            const auto position = state::from_pgn(
                "[Board \"custom\"]\n[Size \"4x4\"]\n"
                "[3k/4/4/KN2:" + std::to_string(source_line) + ":1:w]\n["
                + destination + ":" + std::to_string(destination_line) + ":1:w]\n");
            auto [info, initial] = HC_info::build_HC(position);
            const index_t source = *info.line_to_axis(source_line);
            const index_t target = *info.line_to_axis(destination_line);
            integer_set departures, arrivals;
            for(index_t i : info.get_universe()[source])
                if(info.get_semimove(source, i).is<departing_move>()) departures.insert(i);
            for(index_t i : info.get_universe()[target])
            {
                const auto sm = info.get_semimove(target, i);
                if(const auto *arrival = sm.get_if<arriving_move>();
                    arrival && arrival->m.from.l() == source_line) arrivals.insert(i);
            }
            assert(departures.size() >= 2 && arrivals.size() >= 2);
            std::vector<integer_set> axes(info.dimension(), integer_set{0});
            axes[source] = departures;
            axes[target] = arrivals;
            const HC rectangle(std::move(axes));
            std::set<moveseq> expected;
            integer_set used_departures;
            std::size_t rejected_checks = 0;
            std::optional<index_t> preferred, rejected;
            for(index_t a : arrivals)
            {
                const auto arrival = info.get_semimove(target, a);
                const auto move = arrival.get<arriving_move>().m;
                bool paired = false;
                for(index_t d : departures)
                {
                    const auto departure = info.get_semimove(source, d);
                    if(departure.get<departing_move>().from != move.from) continue;
                    assert(!paired);
                    paired = true;
                    used_departures.insert(d);
                    point p(info.dimension(), 0);
                    p[source] = d;
                    p[target] = a;
                    assert(!info.jump_order_consistent(p, info.get_universe()));
                    assert(!info.test_present(p, info.get_universe()));
                    if(auto problem = info.find_checks(p, info.get_universe()))
                    {
                        ++rejected_checks;
                        if(std::string(destination) == "3k/4/4/rN1K")
                        {
                            assert(problem->get_fixed_axes().contains(source));
                            assert(problem->get_fixed_axes().contains(target));
                            rejected = a;
                        }
                        continue;
                    }
                    if(move.from.x() == 0 && move.from.y() == 0
                        && move.to.x() == 0 && move.to.y() == 0) preferred = a;
                    auto moves = info.to_action(p);
                    assert(position.can_apply(action::from_moveseq(moves, position)));
                    assert(expected.insert(std::move(moves)).second);
                }
                assert(paired);
            }
            assert(used_departures.size() >= 2 && !expected.empty());
            if(std::string(destination) == "3k/4/4/rN1K") assert(rejected_checks > 0);
            const auto collect = [&](auto actions) {
                std::set<moveseq> actual;
                while(auto moves = actions.first())
                {
                    assert(position.can_apply(action::from_moveseq(*moves, position)));
                    assert(actual.insert(std::move(*moves)).second);
                }
                assert(actual == expected);
            };
            const search_space restricted{rectangle};
            collect(info.search<natural_HC_ordering, hc_policy::iterative>(restricted));
            collect(info.search<natural_HC_ordering, hc_policy::stable>(restricted));
            collect(info.search<natural_HC_ordering, hc_policy::balanced>(restricted));
            collect(info.search<natural_HC_ordering, hc_policy::deferred>(restricted));
            if(std::string(destination) == "3k/4/4/rN1K")
            {
                // Yield a king capture first, then force a knight departure
                // exposing the source king before the remaining legal king move.
                // The learned source and arrival restrictions must be intersected.
                assert(preferred && rejected);
                paired_projection_order order{info.get_universe(), target, *preferred, *rejected};
                collect(info.search<decltype(order), hc_policy::iterative>(restricted, order));
                collect(info.search<decltype(order), hc_policy::stable>(restricted, order));
                collect(info.search<decltype(order), hc_policy::balanced>(restricted, order));
                collect(info.search<decltype(order), hc_policy::deferred>(restricted, order));
            }
            for(unsigned seed : {7, 42})
            {
                std::mt19937 rng(seed);
                random_HC_ordering order(info.get_universe(), rng);
                collect(info.search<decltype(order), hc_policy::iterative>(restricted, order));
                collect(info.search<decltype(order), hc_policy::stable>(restricted, order));
                collect(info.search<decltype(order), hc_policy::balanced>(restricted, order));
                collect(info.search<decltype(order), hc_policy::deferred>(restricted, order));
            }
        }
    }
}

struct retained_slice_order
{
    index_t watched_axis;
    integer_set excluded;
    std::size_t *visits;

    const integer_set &operator()(index_t axis, const integer_set &allowed) const
    {
        if(axis == watched_axis && allowed.intersects(excluded)) ++*visits;
        return allowed;
    }
};

void test_persistent_retained_slice()
{
    const auto position = state::from_pgn(
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:-2:3:b]\n"
        "[3k/4/4/KN2:-1:5:w]\n[3k/4/4/KN2:-1:5:b]\n"
        "[3k/4/4/KN2:-1:6:w]\n[3k/4/4/KN2:0:5:w]\n");
    auto [info, space] = HC_info::build_HC(position);
    const HC origin = space.back();
    const auto problem = info.test_present(point(info.dimension(), 0), info.get_universe());
    assert(problem && !info.get_universe().is_slice_good(*problem));
    assert(origin.is_slice_good(*problem));
    const auto &fixed = problem->get_fixed_axes();
    const auto cut = std::find_if(fixed.begin(), fixed.end(), [&](const auto &axis) {
        return !origin[axis.first].is_subset_of(axis.second);
    });
    assert(cut != fixed.end());
    const search_space restricted{origin};
    std::set<moveseq> expected;
    for(const auto &moves : info.search<natural_HC_ordering, hc_policy::iterative>(restricted)) expected.insert(moves);
    assert(!expected.empty());
    std::size_t visits = 0;
    retained_slice_order order{cut->first, cut->second, &visits};
    std::set<moveseq> actual;
    for(const auto &moves : info.search<decltype(order), hc_policy::deferred>(restricted, order))
        assert(actual.insert(moves).second);
    assert(actual == expected);
    // The first candidate discovers this globally non-unary problem. Its
    // locally unary cut then persists across every subsequent yielded action.
    assert(visits == 1);
}

void test_reactivated_present()
{
    // Earlier reactivated endpoints excuse the pass on L0; endpoints on the
    // same turn do not, because White precedes Black within that turn.
    for(int reactivated_t : {3, 5})
    {
        for(bool playable : {false, true})
        {
            const std::string pgn =
                "[Board \"custom\"]\n[Size \"4x4\"]\n"
                "[3k/4/4/KN2:-2:" + std::to_string(reactivated_t)
                + (playable ? ":w]\n" : ":b]\n")
                + "[3k/4/4/KN2:-1:5:w]\n"
                  "[3k/4/4/KN2:-1:5:b]\n"
                  "[3k/4/4/KN2:-1:6:w]\n"
                  "[3k/4/4/KN2:0:5:w]\n";
            const state initial = state::from_pgn(pgn);
            auto [info, space] = HC_info::build_HC(initial);
            const auto collect = [&](auto actions) {
                std::set<moveseq> result;
                for(const moveseq &moves : actions)
                {
                    std::vector<ext_move> extended;
                    for(const full_move &move : moves) extended.emplace_back(move, initial);
                    assert(initial.can_apply(action::from_vector(extended, initial)));
                    result.insert(moves);
                }
                return result;
            };
            const auto expected = collect(info.search<natural_HC_ordering, hc_policy::iterative>(space));
            const size_t expected_count = reactivated_t == 3
                ? (playable ? 784 : 150) : (playable ? 3644 : 146);
            assert(expected.size() == expected_count);
            assert((collect(info.search<natural_HC_ordering, hc_policy::balanced>(space)) == expected));
            assert((collect(info.search<natural_HC_ordering, hc_policy::stable>(space)) == expected));
            assert((collect(info.search<natural_HC_ordering, hc_policy::deferred>(space)) == expected));
            if(reactivated_t == 5 && playable)
            {
                // Global problems must also preserve the complete action set
                // across a highly fragmented, disjoint refinement.
                std::deque<HC> regions;
                for(const HC &region : space)
                    if(!region.empty()) regions.push_back(region);
                while(regions.size() < 1025)
                {
                    HC hc = std::move(regions.front());
                    regions.pop_front();
                    index_t axis = 0;
                    while(axis < hc.dimension() && hc[axis].size() <= 1) ++axis;
                    if(axis == hc.dimension())
                    {
                        regions.push_back(std::move(hc));
                        continue;
                    }
                    const index_t coordinate = *hc[axis].begin();
                    auto [with_coordinate, without_coordinate] = hc.split(axis, coordinate);
                    regions.push_back(std::move(with_coordinate));
                    regions.push_back(std::move(without_coordinate));
                }
                search_space fragmented;
                for(HC &region : regions) fragmented.push_back(std::move(region));
                assert((collect(info.search<natural_HC_ordering, hc_policy::deferred>(fragmented)) == expected));
                assert((collect(info.search<natural_HC_ordering, hc_policy::balanced>(std::move(fragmented))) == expected));
            }
            for(unsigned seed : {0, 1, 2})
            {
                std::mt19937 rng(seed);
                random_HC_ordering order(info.get_universe(), rng);
                assert((collect(info.search<decltype(order), hc_policy::balanced>(space, order)) == expected));
                assert((collect(info.search<decltype(order), hc_policy::iterative>(space, order)) == expected));
                assert((collect(info.search<decltype(order), hc_policy::deferred>(space, order)) == expected));
            }
            moveseq witness;
            if(playable) witness.emplace_back("(-2T" + std::to_string(reactivated_t) + ")a1a2");
            witness.emplace_back("(-1T6)b1>>(-1T5)b3");
            assert(expected.contains(witness) == (reactivated_t == 3));

            // Fixed coordinate sets must stay within the current HC so plain
            // remove_slice() cannot introduce coordinates from other regions.
            // Keep the effective codimension at one.
            const point no_moves(info.dimension(), 0);
            assert(space.back().contains(no_moves));
            const auto problem = info.test_present(no_moves, space.back());
            assert(problem);
            const auto global_problem = info.test_present(no_moves, info.get_universe());
            assert(global_problem && global_problem->contains(no_moves));
            for(const auto &[n, coordinates] : problem->get_fixed_axes())
                assert(coordinates.is_subset_of(space.back()[n]));
            assert(space.back().is_slice_good(*problem, 1));
            point branched = no_moves;
            for(index_t n = 0; n <= info.first_branch_axis(); ++n)
            {
                for(index_t i : info.get_universe()[n])
                {
                    const semimove sm = info.get_semimove(n, i);
                    for(const full_move &move : witness)
                    {
                        if(const auto *physical = sm.get_if<physical_move>();
                           physical && physical->m == move) branched[n] = i;
                        if(const auto *departure = sm.get_if<departing_move>();
                           departure && departure->from == move.from
                           && (move.from.t() != move.to.t() || move.from.l() != move.to.l()))
                            branched[n] = i;
                        if(const auto *arrival = sm.get_if<arriving_move>();
                           arrival && arrival->m == move) branched[n] = i;
                    }
                }
            }
            assert(!space.back().contains(branched));
            assert(!info.jump_order_consistent(branched, info.get_universe()));
            assert(info.to_action(branched) == witness);
            // Unrestricted axes can cover points outside hc. Such points may
            // be included when invalid, but a legal witness must be excluded.
            assert(!problem->contains(branched) || !expected.contains(witness));
            assert(!global_problem->contains(branched) || !expected.contains(witness));
        }
    }
}

void test_physical_first()
{
    for(const std::string pgn : {
        "[Board \"Standard - Turn Zero\"]\n",
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:1:w]\n",
        "[Board \"custom\"]\n[Size \"4x4\"]\n"
        "[3k/4/4/KN2:0:1:w]\n[3k/4/4/KN2:1:2:w]\n"})
    {
        const state position = state::from_pgn(pgn);
        auto [info, space] = HC_info::build_HC(position);
        const auto [mandatory, optional, unplayable] = position.get_timeline_status();
        std::set<moveseq> expected;
        point p(info.dimension(), 0);
        const auto enumerate = [&](auto &&self, index_t n) -> void {
            if(n == mandatory.size())
            {
                // Use all structural checks as the independent reference.
                if(space.contains(p) && !info.find_problem(p, info.get_universe()))
                    expected.insert(info.to_action(p));
                return;
            }
            for(index_t i : info.get_universe()[n])
                if(info.get_semimove(n, i).is<physical_move>())
                {
                    p[n] = i;
                    self(self, n + 1);
                }
        };
        enumerate(enumerate, 0);
        assert(!expected.empty());
        const auto collect = [&](auto actions) {
            std::set<moveseq> physical, all;
            bool left_physical = false;
            for(const moveseq &moves : actions)
            {
                assert(position.can_apply(action::from_moveseq(moves, position)));
                assert(all.insert(moves).second);
                if(expected.contains(moves))
                {
                    assert(!left_physical);
                    physical.insert(moves);
                }
                else left_physical = true;
            }
            assert(physical == expected);
            return all;
        };
        const auto reference = collect(info.search<natural_HC_ordering, hc_policy::iterative>(space));
        assert((collect(info.search<natural_HC_ordering, hc_policy::balanced>(space)) == reference));
        assert((collect(info.search<natural_HC_ordering, hc_policy::stable>(space)) == reference));
        assert((collect(info.search<natural_HC_ordering, hc_policy::deferred>(space)) == reference));
        // A singleton physical region must include its unvalidated seed.
        p.assign(info.dimension(), 0);
        for(index_t n = 0; n < mandatory.size(); ++n)
            for(index_t i : info.get_universe()[n])
                if(info.get_semimove(n, i).is<physical_move>()) { p[n] = i; break; }
        std::vector<integer_set> axes;
        for(index_t i : p) axes.push_back({i});
        auto actions = info.search(search_space{HC(std::move(axes))});
        const auto first = actions.first();
        assert(first.has_value() == !info.find_problem(p, info.get_universe()).has_value());
        assert(!actions.first());
    }
}

void test_slow_global_conflicts()
{
    const auto path = std::filesystem::path(__FILE__).parent_path()
        / "pgn/gigantic/flat-ucb-match-123.5dpgn";
    std::ifstream input(path);
    assert(input);
    std::ostringstream text;
    text << input.rdbuf();
    const state position = state::from_pgn(text.str());
    assert(position.get_boards().size() == 365);
    {
        auto [info, space] = HC_info::build_HC(position);
        assert((!info.search<natural_HC_ordering, hc_policy::deferred>(std::move(space)).first()));
    }
    for(bool stable : {false, true})
    {
        auto [info, space] = HC_info::build_HC(position);
        const auto start = std::chrono::steady_clock::now();
        auto actions = stable ? info.search<natural_HC_ordering, hc_policy::stable>(std::move(space))
                              : info.search<natural_HC_ordering, hc_policy::balanced>(std::move(space));
        assert(!actions.first());
        std::cerr << (stable ? "stable" : "balanced") << " slow fixture: "
                  << std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - start).count()
                  << " ms\n";
    }
    {
        auto [info, space] = HC_info::build_HC(position);
        test_balanced_stable_transition(info, std::move(space));
    }
    assert(position.has_phantom_check());
    assert(position.get_mate_type() == mate_type::CHECKMATE);
}

int run()
{
    test_hc();
    test_remove_point_partition();
    test_consuming_slice_partition();
    test_slice_good();
    test_backward_slice_removal();
    test_backward_slice_order();
    test_global_slice_order();
    test_search_branch_order();
    test_line_axis_lookup();
    test_point_workspace();
    test_negative_point_selection();
    test_paired_similar_rectangle();
    test_persistent_retained_slice();
    test_reactivated_present();
    test_physical_first();
    test_slow_global_conflicts();
    //test_remove();
//    HC h2 = {{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}}};
//    point b = {20};
//    std::cout << h2.contains(b) << "\n";
//    search_space ss3 = h2.remove_point(b);
//    std::cout << ss3.to_string() << "\n";
    std::cerr << "---= test_hcutils.cpp: all passed =---" << std::endl;
    return 0;
}
} /* namespace hcutils_tests */

namespace graph_tests
{
namespace
{
using adjacency_matrix = std::vector<std::vector<bool>>;
using matching = std::vector<std::pair<index_t, index_t>>;

void require(bool condition)
{
    if(!condition)
    {
        std::abort();
    }
}

bool brute_force_match(
    const adjacency_matrix &adjacency,
    const std::vector<bool> &required,
    std::vector<bool> &matched)
{
    std::size_t vertex = 0;
    while(vertex < required.size()
          && (!required[vertex] || matched[vertex]))
    {
        ++vertex;
    }
    if(vertex == required.size())
    {
        return true;
    }

    matched[vertex] = true;
    for(std::size_t other = 0; other < adjacency.size(); ++other)
    {
        if(adjacency[vertex][other] && !matched[other])
        {
            matched[other] = true;
            if(brute_force_match(adjacency, required, matched))
            {
                matched[vertex] = false;
                matched[other] = false;
                return true;
            }
            matched[other] = false;
        }
    }
    matched[vertex] = false;
    return false;
}

bool brute_force_match(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required_vertices)
{
    std::vector<bool> required(adjacency.size(), false);
    for(index_t vertex : required_vertices)
    {
        required[vertex] = true;
    }
    std::vector<bool> matched(adjacency.size(), false);
    return brute_force_match(adjacency, required, matched);
}

void validate_matching(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required,
    const matching &result)
{
    std::vector<bool> matched(adjacency.size(), false);
    for(const auto &[u, v] : result)
    {
        require(u < adjacency.size());
        require(v < adjacency.size());
        require(u != v);
        require(adjacency[u][v]);
        require(!matched[u]);
        require(!matched[v]);
        matched[u] = true;
        matched[v] = true;
    }
    for(index_t vertex : required)
    {
        require(matched[vertex]);
    }
}

void compare_with_brute_force(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required, const matching &initial = {},
    graph::workspace *ws = nullptr)
{
    graph tested(static_cast<index_t>(adjacency.size()));
    for(index_t u = 0; u < adjacency.size(); ++u)
    {
        for(index_t v = 0; v < u; ++v)
        {
            if(adjacency[u][v])
            {
                tested.add_edge(u, v);
            }
        }
    }

    const bool expected = brute_force_match(adjacency, required);
    graph::workspace fresh;
    const std::optional<matching> actual = tested.find_matching(required, fresh, initial);
    if(ws) require(tested.find_matching(required, *ws, initial) == actual);
    if(actual.has_value() != expected)
    {
        std::cerr << "matching disagreement: expected=" << expected
                  << ", actual=" << actual.has_value() << "\nrequired:";
        for(index_t vertex : required)
        {
            std::cerr << ' ' << vertex;
        }
        std::cerr << "\nedges:\n";
        for(index_t u = 0; u < adjacency.size(); ++u)
        {
            for(index_t v = 0; v < u; ++v)
            {
                if(adjacency[u][v])
                {
                    std::cerr << u << " -- " << v << '\n';
                }
            }
        }
        std::abort();
    }
    if(actual)
    {
        validate_matching(adjacency, required, *actual);
    }
}

adjacency_matrix make_graph(
    std::size_t size,
    std::initializer_list<std::pair<index_t, index_t>> edges)
{
    adjacency_matrix adjacency(size, std::vector<bool>(size, false));
    for(const auto &[u, v] : edges)
    {
        adjacency[u][v] = adjacency[v][u] = true;
    }
    return adjacency;
}

void test_existing_examples()
{
    compare_with_brute_force(make_graph(7, {
        {0, 1}, {1, 2}, {3, 4}, {4, 5}, {5, 6}, {6, 0}
    }), {0, 2, 5, 6});

    compare_with_brute_force(make_graph(10, {
        {2, 1}, {3, 2}, {4, 2}, {5, 0}, {5, 1}, {5, 2}, {5, 3},
        {5, 4}, {6, 3}, {7, 0}, {7, 4}, {8, 0}, {8, 3}, {8, 4}
    }), {0, 1, 2, 5, 6, 7, 8});
}

void test_blossom_regression()
{
    compare_with_brute_force(make_graph(10, {
        {3, 0}, {4, 2}, {5, 0}, {5, 1}, {5, 3}, {6, 2}, {6, 5},
        {7, 0}, {7, 2}, {7, 3}, {7, 4}, {7, 5}, {7, 6}, {8, 3},
        {9, 3}, {9, 4}
    }), {0, 1, 2, 3, 4, 5, 6, 7, 9});
}

void test_257_vertices()
{
    adjacency_matrix adjacency(257, std::vector<bool>(257, false));
    adjacency[255][256] = adjacency[256][255] = true;
    compare_with_brute_force(adjacency, {255, 256});
    compare_with_brute_force(adjacency, {255, 256}, {{255, 256}});
}

void test_seeded_matching()
{
    // Cover a newly mandatory vertex by freeing an optional vertex, without
    // increasing matching cardinality.
    compare_with_brute_force(make_graph(3, {{0, 1}, {1, 2}}), {1, 2}, {{0, 1}});
    // Retain one edge, discard a disappeared edge, then repair the coverage.
    compare_with_brute_force(make_graph(4, {{0, 1}, {2, 3}}),
        {0, 1, 2, 3}, {{0, 1}, {1, 2}});
    // A seed from a larger graph must not access vertices outside this graph.
    compare_with_brute_force(make_graph(2, {{0, 1}}), {0}, {{0, 3}, {0, 1}});
    // Existing blossom regression, starting with an arbitrary partial matching.
    compare_with_brute_force(make_graph(10, {
        {3, 0}, {4, 2}, {5, 0}, {5, 1}, {5, 3}, {6, 2}, {6, 5},
        {7, 0}, {7, 2}, {7, 3}, {7, 4}, {7, 5}, {7, 6}, {8, 3},
        {9, 3}, {9, 4}
    }), {0, 1, 2, 3, 4, 5, 6, 7, 9}, {{0, 3}, {2, 4}, {5, 6}});
    graph empty_required(2);
    empty_required.add_edge(0, 1);
    const matching seed{{0, 1}};
    graph::workspace ws;
    const auto result = empty_required.find_matching({}, ws, seed);
    require(result && result->empty());
    graph optional_edges(4);
    optional_edges.add_edge(0, 1);
    optional_edges.add_edge(2, 3);
    const matching optional_seed{{2, 3}};
    const auto trimmed = optional_edges.find_matching({0}, ws, optional_seed);
    require(trimmed && trimmed->size() == 1);
    validate_matching(make_graph(4, {{0, 1}, {2, 3}}), {0}, *trimmed);
}

void test_random_graphs()
{
    graph::workspace ws;
    std::mt19937 random(0x5dca55u);
    std::bernoulli_distribution has_edge(0.35);
    std::bernoulli_distribution is_required(0.55);

    for(index_t dimension = 1; dimension <= 10; ++dimension)
    {
        for(int sample = 0; sample < 200; ++sample)
        {
            adjacency_matrix adjacency(
                dimension, std::vector<bool>(dimension, false));
            for(index_t u = 0; u < dimension; ++u)
            {
                for(index_t v = 0; v < u; ++v)
                {
                    adjacency[u][v] = adjacency[v][u] = has_edge(random);
                }
            }

            std::vector<index_t> required;
            for(index_t vertex = 0; vertex < dimension; ++vertex)
            {
                if(is_required(random))
                {
                    required.push_back(vertex);
                }
            }
            compare_with_brute_force(adjacency, required, {}, &ws);
            // Seeds need not be valid in the current graph. Filter their
            // surviving edges and compare feasibility against exhaustive search.
            matching seed;
            std::vector<index_t> vertices(dimension);
            std::iota(vertices.begin(), vertices.end(), index_t{0});
            std::shuffle(vertices.begin(), vertices.end(), random);
            for(std::size_t i = 0; i + 1 < vertices.size(); i += 2)
                seed.emplace_back(vertices[i], vertices[i + 1]);
            compare_with_brute_force(adjacency, required, seed, &ws);
        }
    }
}

void test_matching_workspace()
{
    graph::workspace ws;
    graph large(257);
    large.add_edge(255, 256);
    const auto saved = large.find_matching({255, 256}, ws);
    require(saved && saved->size() == 1);
    const auto snapshot = *saved;

    graph small(2);
    require(!small.find_matching({0}, ws, *saved));
    const auto empty = small.find_matching({}, ws, *saved);
    require(empty && empty->empty());
    small.add_edge(0, 1);
    const auto repaired = small.find_matching({0, 1}, ws, *saved);
    require(repaired.has_value());
    graph::workspace fresh;
    require(repaired == small.find_matching({0, 1}, fresh));
    require(large.find_matching({255, 256}, ws, *repaired) == saved);
    require(*saved == snapshot);
}
}

int run()
{
    test_existing_examples();
    test_blossom_regression();
    test_257_vertices();
    test_seeded_matching();
    test_matching_workspace();
    test_random_graphs();
    std::cerr << "---= test_graph.cpp: all passed =---\n";
    return 0;
}
} /* namespace graph_tests */

int main()
{
    std::cerr << "Running hcutils tests\n";
    if(const int result = hcutils_tests::run(); result != 0)
        return result;
    std::cerr << "Running graph tests\n";
    if(const int result = graph_tests::run(); result != 0)
        return result;
    return 0;
}
