#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <iterator>
#include <cmath>
#include <string>
#include "hypercuboid.h"
#include "move_evaluation.h"
#include "pgnparser.h"
#include <cstddef>
#include <iostream>

namespace move_evaluation_tests
{
namespace
{
state position(std::string board)
{
    return state::from_pgn<true>(
        "[Size \"4x4\"]\n[Board \"custom\"]\n[" + board + ":0:1:w]\n");
}

state capture_position(char victim)
{
    return state::from_pgn<true>(
        "[Size \"8x8\"]\n[Board \"custom\"]\n"
        "[7k/8/8/8/8/8/1" + std::string(1, victim) + "6/KR6:0:1:w]\n");
}

index_t coordinate_for(const HC_info &info, const full_move &wanted)
{
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
        for(index_t coordinate : info.get_universe()[axis])
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

    const auto &weights = default_move_evaluation_weights.values;
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
    for(const auto &[victim, feature] : cases)
    {
        const state before = capture_position(victim);
        auto [info, space] = HC_info::build_HC(before);
        (void)space;
        const auto coordinate = coordinate_for(info, full_move("(0T1)b1b2"));
        move_evaluation_weights weights{};
        weights.values[feature] = 13.0f;
        assert(move_evaluator(info, weights).score(0, coordinate) == 13.0f);
    }

    const state black_to_move = state::from_pgn<true>(
        "[Size \"8x8\"]\n[Board \"custom\"]\n"
        "[7k/8/8/8/8/8/1P6/Kr6:0:1:b]\n");
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

void test_mvvlva_capture_ordering()
{
    const auto score = [](char attacker, char victim, bool black = false,
                          bool unmoved = false) {
        const state before = state::from_pgn<true>(
            "[Size \"8x8\"]\n[Board \"custom\"]\n"
            "[7k/8/8/8/8/8/1" + std::string(1, victim) + "6/K"
            + std::string(1, attacker) + (unmoved ? "*" : "")
            + "6:0:1:" + (black ? "b" : "w") + "]\n");
        auto [info, space] = HC_info::build_HC(before);
        (void)space;
        const auto coordinate = coordinate_for(info, full_move("(0T1)b1b2"));
        // Check behavior is covered separately; isolate capture ordering here.
        return move_evaluator(info, capture_weights, capture_ordering::mvvlva)
            .score(0, coordinate);
    };

    // The cheapest attacker wins ties, but never outweighs the victim rank.
    const char victims[] = {'p', 'w', 'r', 'b', 'u', 's', 'q'};
    float previous_best = 0.0f;
    for(char victim : victims)
    {
        const float rook = score('R', victim);
        const float queen = score('Q', victim);
        assert(rook > queen);
        assert(queen > previous_best);
        assert(rook >= 480.0f && rook < 780.0f);
        previous_best = rook;
    }
    assert(score('R', 'r') == score('R', 'c'));
    assert(score('R', 'b') == score('R', 'n'));
    assert(score('R', 'u') == score('R', 'd'));
    assert(score('R', 'q') == score('R', 'y'));
    assert(score('R', 'p') == score('r', 'P', true));
    assert(score('R', 'p') == score('R', 'p', false, true));

    const state before = position("3k/4/1p2/1R1K");
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    move_evaluator evaluator(info, capture_check_weights, capture_ordering::mvvlva);
    assert(evaluator.score(0, coordinate_for(info, full_move("(0T1)b1c1"))) == 0.0f);
    std::stop_source stopped;
    stopped.request_stop();
    assert(!evaluator.build_score_table(stopped.get_token()));
}

void test_local_capture_exposure()
{
    const auto verify = [](const state &before, const char *notation,
                           float penalty, float promotion_bonus = 0.0f) {
        auto [info, space] = HC_info::build_HC(before);
        (void)space;
        const full_move wanted(notation);
        move_evaluator baseline(info, capture_weights, capture_ordering::mvvlva);
        move_evaluator exposure(info, capture_weights, capture_ordering::mvvlva_exposure);
        move_evaluator checking(info, capture_check_weights,
                                capture_ordering::mvvlva_exposure);
        semimove_feature features(info);
        const auto table = checking.build_score_table();
        assert(table);
        bool found = false;
        for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
            for(index_t coordinate : info.get_universe()[axis])
            {
                const auto boards = info.get_move_boards(axis, coordinate);
                if(!boards)
                {
                    assert(exposure.score(axis, coordinate) == 0.0f);
                    continue;
                }
                if(boards->move != wanted) continue;
                const float score = exposure.score(axis, coordinate);
                assert(std::abs(baseline.score(axis, coordinate) - score
                                - penalty + promotion_bonus) < 0.001f);
                assert((*table)[axis][coordinate]
                    == score + (features.is_check(axis, coordinate) ? 300.0f : 0.0f));
                if(wanted.captured_piece(before) != NO_PIECE)
                {
                    assert(score > 348.0f && score <= 631.0f);
                    assert(score + 300.0f > 631.0f);
                }
                found = true;
            }
        assert(found);
        std::stop_source stopped;
        stopped.request_stop();
        assert(!checking.build_score_table(stopped.get_token()));
    };
    const auto setup = [](const char *fen, bool black = false) {
        return state::from_pgn<true>("[Board \"custom\"]\n[" + std::string(fen)
            + ":0:1:" + (black ? "b" : "w") + "]");
    };
    // An unprotected victim, a friendly rook, and a hostile rook respectively.
    verify(setup("7k/8/8/8/8/8/1p6/1Q5K"), "(0T1)b1b2", 0.0f);
    verify(setup("7k/8/8/8/1R6/8/1p6/1Q5K"), "(0T1)b1b2", 0.0f);
    verify(setup("7k/8/8/8/1r6/8/1p6/1Q5K"), "(0T1)b1b2", 128.0f);
    // Local pawn and rook defenders suppress the penalty, without simulating exchanges.
    verify(setup("7k/8/8/8/1r6/8/1p6/PQ5K"), "(0T1)b1b2", 0.0f);
    verify(setup("7k/8/8/8/1R6/2p5/1p6/1Q5K"), "(0T1)b1b2", 0.0f);
    verify(setup("1q5k/1P6/8/1R6/8/8/8/7K", true), "(0T1)b8b7", 128.0f);
    verify(setup("1qp4k/1P6/8/1R6/8/8/8/7K", true), "(0T1)b8b7", 0.0f);

    // Vacating the source reveals a new sliding attack on the destination.
    const state xray = setup("7k/8/8/8/1r6/1Q6/1p6/7K");
    assert(!xray.get_board_ptr(0, 1, false)->is_under_attack(ppos(1,1), false));
    verify(xray, "(0T1)b3b2", 128.0f);
    // Smaller potential losses get smaller penalties; equal/favorable trades get none.
    verify(setup("7k/8/8/8/1r6/8/1p6/1R5K"), "(0T1)b1b2", 20.0f);
    verify(setup("7k/8/8/8/1r6/8/1r6/1R5K"), "(0T1)b1b2", 0.0f);
    verify(setup("7k/8/8/8/2r5/8/2q5/1P5K"), "(0T1)b1c2", 0.0f);
    verify(setup("7k/8/8/8/1r6/8/8/1Q5K"), "(0T1)b1b2", 0.0f);
    // Pawn direction is evaluated for the arriving piece's color.
    verify(setup("7k/8/8/8/8/2p5/1p6/1Q5K"), "(0T1)b1b2", 128.0f);
    verify(setup("7k/8/8/8/8/2P5/1p6/1Q5K"), "(0T1)b1b2", 0.0f);
    // Exposure uses the promoted queen value, rather than the departing pawn.
    verify(setup("1r5k/P7/1r6/8/8/8/8/7K"), "(0T1)a7b8", 114.0f, 48.0f);
    verify(setup("1r5k/P7/8/8/8/8/8/7K"), "(0T1)a7b8", 0.0f, 48.0f);
    verify(setup("7k/P7/8/8/8/8/8/7K"), "(0T1)a7a8", 0.0f, 48.0f);
    verify(setup("7k/8/8/8/8/1R6/p7/1R5K", true), "(0T1)a2b1", 114.0f, 48.0f);
    multiverse underpromotion(timeline_layout::odd, {
        {0, 1, false, "1r5k/P7/1r6/8/8/8/8/7K"},
    });
    verify(state(underpromotion, promotion_options::KNIGHT), "(0T1)a7b8", 5.0f, 25.0f);

    multiverse brawn_promotion(timeline_layout::odd, {
        {0, 1, false, "1r5k/W7/1r6/8/8/8/8/7K"},
    });
    verify(state(brawn_promotion, promotion_options::KNIGHT), "(0T1)a7b8", 5.0f, 23.0f);

    multiverse jumps(timeline_layout::odd, {
        {-1, 1, false, "7k/8/8/8/1r6/8/8/1p5K"},
        {0, 1, false, "7k/8/8/8/8/8/8/1Q5K"},
    });
    verify(state(jumps), "(0T1)b1(-1T1)b1", 128.0f);

    // No local attack on arrival: another timeline's rook is outside this feature.
    multiverse remote(timeline_layout::odd, {
        {-1, 1, true, "7k/8/8/8/8/8/1r6/7K"},
        {0, 1, false, "7k/8/8/8/8/8/1p6/1Q5K"},
    });
    verify(state(remote), "(0T1)b1b2", 0.0f);
}

void test_king_branch_scoring()
{
    const state before = state::from_pgn<true>(
        "[Size \"8x8\"]\n[Board \"custom\"]\n"
        "[7k/8/8/8/8/8/1K6/1R6:0:1:w]\n"
        "1. Rb1c1 / Kh8g8\n");
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    const full_move wanted("(0T2)b2(0T1)c3");
    bool found = false;
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
        for(index_t coordinate : info.get_universe()[axis])
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
    assert(table && table->size() == info.get_universe().dimension());
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
    {
        index_t largest = 0;
        bool nonempty = false;
        for(index_t coordinate : info.get_universe()[axis])
        {
            nonempty = true;
            largest = std::max(largest, coordinate);
            assert((*table)[axis][coordinate] == evaluator.score(axis, coordinate));
        }
        if(nonempty)
        {
            assert((*table)[axis].size() == static_cast<std::size_t>(largest) + 1);
            for(index_t coordinate = 0; coordinate <= largest; ++coordinate)
                if(!info.get_universe()[axis].contains(coordinate))
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

void test_superphysical_scoring()
{
    multiverse boards(timeline_layout::odd, {
        {-1, 1, false, "r6k/8/8/8/8/8/8/K7"},
        {0, 1, false, "7k/P7/8/8/8/8/8/K7"},
    });
    const state before(boards, promotion_options::QUEEN);
    auto [info, space] = HC_info::build_HC(before);
    (void)space;
    bool found = false;
    for(index_t axis = 0; axis < info.get_universe().dimension() && !found; ++axis)
        for(index_t coordinate : info.get_universe()[axis])
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
    const auto verify = [](const std::string &board, const char *move,
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

int run()
{
    test_weight_layout_and_profiles();
    test_capture_scoring_and_weight_copy();
    test_capture_groups();
    test_check_and_combined_scoring();
    test_mvvlva_capture_ordering();
    test_local_capture_exposure();
    test_king_branch_scoring();
    test_score_table_and_cancellation();
    test_superphysical_scoring();
    test_latent_threat_weight_dependencies();
    return 0;
}
} /* namespace move_evaluation_tests */

namespace hc_move_evaluation_tests
{
namespace
{
struct coverage
{
    std::size_t moves = 0;
    std::size_t arrivals = 0;
    std::size_t departures = 0;
    std::size_t nulls = 0;
    bool check = false;
    bool jump = false;
    bool promotion = false;
};

state parse(const std::string &pgn)
{
    return state::from_pgn<true>(pgn);
}

void compare(const state &position, coverage &seen)
{
    auto [info, space] = HC_info::build_HC(position);
    (void)space;
    semimove_feature evaluator(info);
    move_evaluator scoring(info, default_move_evaluation_weights);
    move_evaluator mvvlva(info, capture_weights, capture_ordering::mvvlva);
    move_evaluator mvvlva_check(info, capture_check_weights, capture_ordering::mvvlva);
    const auto mvvlva_table = mvvlva_check.build_score_table();
    assert(mvvlva_table);
    move_evaluation_weights jump_weights{};
    jump_weights.values[move_evaluation_weights::SUPERPHYSICAL] = 1.0f;
    move_evaluator jump_scoring(info, jump_weights);
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
    {
        for(index_t coordinate : info.get_universe()[axis])
        {
            const semimove candidate = info.get_semimove(axis, coordinate);
            const auto cached = info.get_move_boards(axis, coordinate);
            if(!cached)
            {
                assert(!evaluator.is_check(axis, coordinate));
                assert(scoring.score(axis, coordinate) == 0.0f);
                assert(jump_scoring.score(axis, coordinate) == 0.0f);
                assert(mvvlva_check.score(axis, coordinate) == 0.0f);
                if(candidate.is<departing_move>()) ++seen.departures;
                else if(candidate.is<null_move>()) ++seen.nulls;
                else assert(false && "unscored coordinate has unexpected kind");
                continue;
            }
            if(candidate.is<arriving_move>())
            {
                assert(jump_scoring.score(axis, coordinate) == 1.0f);
                ++seen.arrivals;
            }
            else
            {
                assert(candidate.is<physical_move>());
                assert(jump_scoring.score(axis, coordinate) == 0.0f);
            }
            state after = position;
            const ext_move prepared(cached->move, position);
            assert(after.apply_move<true>(prepared));
            const bool checking = evaluator.is_check(axis, coordinate);
            const float capture_score = mvvlva.score(axis, coordinate);
            const bool capture = cached->move.captured_piece(position) != NO_PIECE;
            assert((capture_score > 0.0f) == capture);
            assert((*mvvlva_table)[axis][coordinate]
                   == capture_score + (checking ? 300.0f : 0.0f));
            // Candidate features scan attacks beyond the active present too.
            check_position check_view(after);
            const auto [lo, hi] = after.get_lines_range();
            for (int l = lo; l <= hi; ++l) {
                const auto at = after.get_timeline_end(l);
                if (at.c() != position.get_present().c())
                    check_view.add_board(l, at.next_turn(),
                        *after.get_board_ptr(l,at.t(),at.c()));
            }
            assert(checking == static_cast<bool>(
                check_view.first_check(position.get_present().c())));
            assert(checking == (evaluator.is_physical_check(axis, coordinate)
                                || evaluator.is_sp_check(axis, coordinate)));
            if (evaluator.is_historical_check(axis, coordinate)) assert(checking);
            ++seen.moves;
            seen.check |= checking;
            seen.jump |= static_cast<bool>(
                cached->move.move_type(position) & special_move_t::SUPERPHYSICAL);
            seen.promotion |= static_cast<bool>(
                cached->move.move_type(position) & special_move_t::PROMOTION);
        }
    }
}

void test_superphysical_and_historical_checks()
{
    const char *source = "7k/8/8/8/8/8/8/1R2K3";
    const char *target = "8/8/8/8/8/8/8/k7";
    const auto verify = [](const state &position, bool historical) {
        auto [info, space] = HC_info::build_HC(position);
        (void)space;
        semimove_feature features(info);
        const full_move checking("(0T1)b1a1");
        const full_move physical("(0T1)b1b8");
        bool found = false;
        bool found_physical = false;
        for (index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
            for (index_t coordinate : info.get_universe()[axis]) {
                const auto boards = info.get_move_boards(axis, coordinate);
                if (!boards) continue;
                if (boards->move == physical) {
                    found_physical = true;
                    assert(features.is_physical_check(axis, coordinate));
                    assert(!features.is_sp_check(axis, coordinate));
                    assert(!features.is_historical_check(axis, coordinate));
                }
                if (boards->move != checking) continue;
                found = true;
                assert(!features.is_physical_check(axis, coordinate));
                assert(features.is_sp_check(axis, coordinate));
                assert(features.is_historical_check(axis, coordinate)
                       == historical);
                assert(features.is_check(axis, coordinate));
            }
        assert(found);
        assert(found_physical);
    };
    multiverse endpoint(timeline_layout::odd, {{0,1,false,source},{1,1,true,target}});
    verify(state(endpoint), false);
    multiverse history(timeline_layout::odd, {{0,1,false,source},{1,2,false,target},
                            {1,2,true,target},{1,3,false,target},
                            {1,3,true,target}});
    verify(state(history), true);
}
}

int run()
{
    test_superphysical_and_historical_checks();
    coverage seen;
    compare(parse("[Size \"4x4\"]\n[3k/4/Kp2/1R2:0:1:w]"), seen);

    multiverse promotion_boards(timeline_layout::odd, {
        {0, 1, false, "7k/P7/8/8/8/8/8/K7"},
    });
    compare(state(promotion_boards, promotion_options::QUEEN), seen);

    multiverse jump_boards(timeline_layout::odd, {
        {-1, 1, false, "r6k/8/8/8/8/8/8/K7"},
        {0, 1, false, "7k/P7/8/8/8/8/8/K7"},
    });
    compare(state(jump_boards, promotion_options::QUEEN), seen);

    state en_passant = parse("[Board \"Standard - Turn Zero\"]");
    for(const char *move : {"(0T1)e2e4", "(0T1)a7a6",
                           "(0T2)e4e5", "(0T2)d7d5"})
    {
        assert(en_passant.apply_move(full_move(move)));
        assert(en_passant.submit());
    }
    compare(en_passant, seen);

    assert(seen.moves > 0);
    assert(seen.arrivals > 0);
    assert(seen.departures > 0);
    assert(seen.nulls > 0);
    assert(seen.check);
    assert(seen.jump);
    assert(seen.promotion);
    return 0;
}
} /* namespace hc_move_evaluation_tests */

int main()
{
    std::cerr << "Running move_evaluation tests\n";
    if(const int result = move_evaluation_tests::run(); result != 0)
        return result;
    std::cerr << "Running hc_move_evaluation tests\n";
    if(const int result = hc_move_evaluation_tests::run(); result != 0)
        return result;
    return 0;
}
