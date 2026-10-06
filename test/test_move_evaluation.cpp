#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <iterator>
#include <cmath>
#include <string>

#include "hypercuboid.h"
#include "move_evaluation.h"
#include "pgnparser.h"

namespace
{
state position(std::string board)
{
    return state(*pgnparser(
        "[Size \"4x4\"]\n[Board \"custom\"]\n[" + board + ":0:1:w]\n")
        .parse_game());
}

state capture_position(char victim)
{
    return state(*pgnparser(
        "[Size \"8x8\"]\n[Board \"custom\"]\n"
        "[7k/8/8/8/8/8/1" + std::string(1, victim) + "6/KR6:0:1:w]\n")
        .parse_game());
}

index_t coordinate_for(const HC_info& info, const full_move& wanted)
{
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
        for(index_t coordinate : info.universe[axis])
        {
            const auto boards = info.get_move_boards(axis, coordinate);
            if(boards && boards->move == wanted) return coordinate;
        }
    assert(false && "requested move not found in HC");
    return 0;
}

void test_weight_layout_and_profiles()
{
    static_assert(move_evaluation_weights::COUNT == 12);
    move_evaluation_weights zero{};
    for(float value : zero.values) assert(value == 0.0f);

    const auto& weights = default_move_evaluation_weights.values;
    assert(weights[move_evaluation_weights::LATENT_KING_THREAT] == -50000.0f);
    assert(weights[move_evaluation_weights::NORMAL_KING_MOVE] == -1200.0f);
    assert(weights[move_evaluation_weights::KING_BRANCH] == -8000.0f);
    assert(weights[move_evaluation_weights::QUEEN_CAPTURE] == 1584.0f);
    assert(weights[move_evaluation_weights::PRINCESS_CAPTURE] == 880.0f);
    assert(weights[move_evaluation_weights::DRAGON_UNICORN_CAPTURE] == 495.0f);
    assert(weights[move_evaluation_weights::BISHOP_KNIGHT_CAPTURE] == 385.0f);
    assert(weights[move_evaluation_weights::ROOK_COMMON_KING_CAPTURE] == 330.0f);
    assert(weights[move_evaluation_weights::BRAWN_CAPTURE] == 132.0f);
    assert(weights[move_evaluation_weights::PAWN_CAPTURE] == 110.0f);
    assert(weights[move_evaluation_weights::CHECK] == 1200.0f);
    assert(weights[move_evaluation_weights::SUPERPHYSICAL] == -700.0f);

    for(index_t i = move_evaluation_weights::QUEEN_CAPTURE;
        i <= move_evaluation_weights::PAWN_CAPTURE; ++i)
    {
        assert(capture_weights.values[i] == 480.0f);
        assert(capture_check_weights.values[i] == 480.0f);
    }
    assert(capture_weights.values[move_evaluation_weights::CHECK] == 0.0f);
    assert(capture_check_weights.values[move_evaluation_weights::CHECK] == 300.0f);
}

void test_capture_scoring_and_weight_copy()
{
    const state before = position("3k/4/1p2/KR2");
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    const auto coordinate = coordinate_for(info, full_move("(0T1)b1b2"));

    move_evaluation_weights weights{};
    weights.values[move_evaluation_weights::PAWN_CAPTURE] = 20.0f;
    move_evaluator evaluator(info, weights);
    weights.values[move_evaluation_weights::PAWN_CAPTURE] = 90.0f;
    assert(evaluator.score(0, coordinate) == 20.0f);

    move_evaluator ordering(info, capture_weights);
    assert(ordering.score(0, coordinate) == 480.0f);
    assert(move_evaluator(info, default_move_evaluation_weights)
           .score(0, coordinate) == 110.0f);
}

void test_capture_groups()
{
    using indices = move_evaluation_weights;
    const std::pair<char, int> cases[] = {
        {'q', indices::QUEEN_CAPTURE}, {'y', indices::QUEEN_CAPTURE},
        {'s', indices::PRINCESS_CAPTURE},
        {'d', indices::DRAGON_UNICORN_CAPTURE},
        {'u', indices::DRAGON_UNICORN_CAPTURE},
        {'b', indices::BISHOP_KNIGHT_CAPTURE},
        {'n', indices::BISHOP_KNIGHT_CAPTURE},
        {'r', indices::ROOK_COMMON_KING_CAPTURE},
        {'c', indices::ROOK_COMMON_KING_CAPTURE},
        {'w', indices::BRAWN_CAPTURE}, {'p', indices::PAWN_CAPTURE},
    };
    for(const auto& [victim, feature] : cases)
    {
        const state before = capture_position(victim);
        auto [info, space] = HC_info::build_HC(before);
        (void)space;
        const auto coordinate = coordinate_for(info, full_move("(0T1)b1b2"));
        move_evaluation_weights weights{};
        weights.values[feature] = 13.0f;
        assert(move_evaluator(info, weights).score(0, coordinate) == 13.0f);
    }

    const state black_to_move(*pgnparser(
        "[Size \"8x8\"]\n[Board \"custom\"]\n"
        "[7k/8/8/8/8/8/1P6/Kr6:0:1:b]\n").parse_game());
    auto [black_info, black_space] = HC_info::build_HC(black_to_move);
    (void)black_space;
    const auto black_capture = coordinate_for(
        black_info, full_move("(0T1)b1b2"));
    move_evaluation_weights pawn_only{};
    pawn_only.values[move_evaluation_weights::PAWN_CAPTURE] = 13.0f;
    assert(move_evaluator(black_info, pawn_only).score(0, black_capture)
           == 13.0f);
}

void test_check_and_combined_scoring()
{
    const state before = position("3k/4/3p/K2R");
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    const auto coordinate = coordinate_for(info, full_move("(0T1)d1d2"));

    move_evaluation_weights check_only{};
    check_only.values[move_evaluation_weights::CHECK] = 7.0f;
    move_evaluator check_evaluator(info, check_only);
    assert(check_evaluator.score(0, coordinate) == 7.0f);

    move_evaluator combined(info, capture_check_weights);
    assert(combined.score(0, coordinate) == 780.0f);
    assert(move_evaluator(info, default_move_evaluation_weights)
           .score(0, coordinate) == 1310.0f);
}

void test_king_branch_scoring()
{
    const state before(*pgnparser(
        "[Size \"8x8\"]\n[Board \"custom\"]\n"
        "[7k/8/8/8/8/8/1K6/1R6:0:1:w]\n"
        "1. Rb1c1 / Kh8g8\n").parse_game());
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    const full_move wanted("(0T2)b2(0T1)c3");
    bool found = false;
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
        for(index_t coordinate : info.universe[axis])
        {
            const auto boards = info.get_move_boards(axis, coordinate);
            if(!boards || boards->move != wanted) continue;
            assert(static_cast<bool>(boards->move.move_type(before)
                                     & special_move_t::BRANCHING));

            move_evaluation_weights weights{};
            weights.values[move_evaluation_weights::KING_BRANCH] = -17.0f;
            weights.values[move_evaluation_weights::SUPERPHYSICAL] = -3.0f;
            assert(move_evaluator(info, weights).score(axis, coordinate)
                   == -20.0f);
            assert(move_evaluator(info, default_move_evaluation_weights)
                   .score(axis, coordinate) == -9900.0f);
            found = true;
        }
    assert(found);
}

void test_score_table_and_cancellation()
{
    const state before = position("3k/4/1p2/KR2");
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    move_evaluator evaluator(info, capture_weights);
    const auto table = evaluator.build_score_table();
    assert(table && table->size() == info.universe.dimension());
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
    {
        index_t largest = 0;
        bool nonempty = false;
        for(index_t coordinate : info.universe[axis])
        {
            nonempty = true;
            largest = std::max(largest, coordinate);
            assert((*table)[axis][coordinate] == evaluator.score(axis, coordinate));
        }
        if(nonempty)
        {
            assert((*table)[axis].size() == static_cast<std::size_t>(largest) + 1);
            for(index_t coordinate = 0; coordinate <= largest; ++coordinate)
                if(!info.universe[axis].contains(coordinate))
                {
                    assert((*table)[axis][coordinate] == 0.0f);
                }
        }
        else
        {
            assert((*table)[axis].empty());
        }
    }
    std::stop_source stopped;
    stopped.request_stop();
    assert(!evaluator.build_score_table(stopped.get_token()));
}

void test_pruned_coordinate_gaps()
{
    const state before = position("3k/4/1p2/KR2");
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    bool pruned = false;
    for(index_t axis = 0; axis < info.universe.dimension() && !pruned; ++axis)
    {
        auto& coordinates = info.universe[axis];
        if(coordinates.size() < 2) continue;
        auto removed = coordinates.begin();
        const index_t gap = *removed;
        const index_t next = *std::next(removed);
        coordinates.erase(gap);
        move_evaluator evaluator(info, capture_weights);
        const auto table = evaluator.build_score_table();
        assert(table);
        assert((*table)[axis].size() > gap);
        assert((*table)[axis][gap] == 0.0f);
        assert((*table)[axis][next] == evaluator.score(axis, next));
        pruned = true;
    }
    assert(pruned);
}

void test_superphysical_scoring()
{
    multiverse_odd boards({
        {-1, 1, false, "r6k/8/8/8/8/8/8/K7"},
        {0, 1, false, "7k/P7/8/8/8/8/8/K7"},
    });
    const state before(boards, promotion_options::QUEEN);
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    bool found = false;
    for(index_t axis = 0; axis < info.universe.dimension() && !found; ++axis)
        for(index_t coordinate : info.universe[axis])
        {
            const auto move = info.get_move_boards(axis, coordinate);
            if(!move || !static_cast<bool>(
                    move->move.move_type(before) & special_move_t::SUPERPHYSICAL))
                continue;
            move_evaluation_weights weights{};
            weights.values[move_evaluation_weights::SUPERPHYSICAL] = -7.0f;
            assert(move_evaluator(info, weights).score(axis, coordinate) == -7.0f);
            found = true;
            break;
        }
    assert(found);
}

void test_latent_threat_weight_dependencies()
{
    const auto verify = [](const std::string& board, const char* move,
                           bool threatened) {
        const state before = position(board);
        auto [info, space] = HC_info::build_HC(before);
        (void)space;
        const auto coordinate = coordinate_for(info, full_move(move));

        move_evaluation_weights both{};
        both.values[move_evaluation_weights::LATENT_KING_THREAT] = -50000.0f;
        both.values[move_evaluation_weights::NORMAL_KING_MOVE] = -1200.0f;
        move_evaluator both_evaluator(info, both);
        assert(both_evaluator.score(0, coordinate)
               == (threatened ? -50000.0f : -1200.0f));

        move_evaluation_weights normal_only{};
        normal_only.values[move_evaluation_weights::NORMAL_KING_MOVE] = -1200.0f;
        move_evaluator normal_evaluator(info, normal_only);
        assert(normal_evaluator.score(0, coordinate)
               == (threatened ? 0.0f : -1200.0f));

        move_evaluation_weights latent_only{};
        latent_only.values[move_evaluation_weights::LATENT_KING_THREAT] = -50000.0f;
        move_evaluator latent_evaluator(info, latent_only);
        assert(latent_evaluator.score(0, coordinate)
               == (threatened ? -50000.0f : 0.0f));
    };

    verify("3k/1K2/b3/4", "(0T1)b3b2", true);
    verify("3k/1K2/4/4", "(0T1)b3b2", false);
}
}

int main()
{
    test_weight_layout_and_profiles();
    test_capture_scoring_and_weight_copy();
    test_capture_groups();
    test_check_and_combined_scoring();
    test_king_branch_scoring();
    test_score_table_and_cancellation();
    test_pruned_coordinate_gaps();
    test_superphysical_scoring();
    test_latent_threat_weight_dependencies();
}
