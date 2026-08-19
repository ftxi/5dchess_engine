#include <cassert>
#include <array>
#include <cmath>
#include <vector>
#include "core/multiverse_variants.h"
#include "core/pgnparser.h"
#include "core/state.h"
#include "core/statistics.h"

constexpr const char *empty_board = "8/8/8/8/8/8/8/8";
constexpr const char *black_king_a1 = "8/8/8/8/8/8/8/k7";
constexpr const char *white_king_a1 = "8/8/8/8/8/8/8/K7";
constexpr const char *black_kings_a1_b1 = "8/8/8/8/8/8/8/kk6";
constexpr const char *white_rooks_a1_b1 = "8/8/8/8/8/8/8/RR6";

void test_standard_material()
{
    const state s(*pgnparser(R"(
[Board "Standard - Turn Zero"]
)").parse_game());

    using data_t = material_data<int>;
    const auto sum = count_material_sum<timelines_status::MANDATORY>(s);
    const auto diff = count_material_diff<timelines_status::MANDATORY>(s);

    const std::array<int, data_t::COUNT> expected_sum{
        16, // lpawn
        4,  // lknight
        6,  // lrook: four rooks and two queens
        6,  // lbishop: four bishops and two queens
        2,  // lunicorn: two queens
        2,  // ldragon: two queens
        2,  // queen
        2   // royal
    };
    assert(sum.values == expected_sum);
    assert((diff.values == std::array<int, data_t::COUNT>{}));
}

void test_material_difference_after_capture()
{
    const state s(*pgnparser(R"(
[Board "Standard - Turn Zero"]

1. e4 / d5
2. exd5
)").parse_game());

    using data_t = material_data<int>;
    const auto sum = count_material_sum<timelines_status::MANDATORY>(s);
    const auto diff = count_material_diff<timelines_status::MANDATORY>(s);

    assert(sum.values[data_t::LPAWN] == 15);
    assert(diff.values[data_t::LPAWN] == -1);
    for(int i = data_t::LKNIGHT; i < data_t::COUNT; ++i)
    {
        assert(diff.values[i] == 0);
    }
}

void test_initial_odd_multiverse()
{
    multiverse_odd multiverse({{0, 1, false, empty_board}});
    const state s(multiverse);
    const auto data = count_timelines(s);

    assert(data.total == 1);
    assert(data.active == 1);
    assert(data.inactive == 0);
    assert(data.mandatory == 1);
    assert(data.optional == 0);
    assert(data.unplayable == 0);
    assert(data.playable == 1);
    assert(data.friendly_created == 0);
    assert(data.hostile_created == 0);
    assert(data.timeline_advantage == 0);
    assert(data.active_timeline_allowance == 1);
    assert(data.friendly_active_created == 0);
    assert(data.hostile_active_created == 0);
}

void test_white_to_move_with_inactive_timeline()
{
    multiverse_odd multiverse({
        {-1, 2, false, empty_board},
        { 0, 1, false, empty_board},
        { 1, 1, false, empty_board},
        { 2, 2, false, empty_board},
        { 3, 2,  true, empty_board},
    });
    const state s(multiverse);
    const auto data = count_timelines(s);

    assert(s.get_present() == turn_t(1, false));
    assert(data.total == 5);
    assert(data.active == 4);
    assert(data.inactive == 1);
    assert(data.mandatory == 2);
    assert(data.optional == 2);
    assert(data.unplayable == 1);
    assert(data.playable == 4);
    assert(data.friendly_created == 3);
    assert(data.hostile_created == 1);
    assert(data.timeline_advantage == -2);
    assert(data.active_timeline_allowance == 0);
    assert(data.friendly_active_created == 2);
    assert(data.hostile_active_created == 1);
}

void test_black_to_move_with_timeline_advantage()
{
    multiverse_even multiverse({
        {-2, 2,  true, empty_board},
        {-1, 1,  true, empty_board},
        { 0, 1,  true, empty_board},
        { 1, 2,  true, empty_board},
        { 2, 2, false, empty_board},
        { 3, 2, false, empty_board},
    });
    const state s(multiverse);
    const auto data = count_timelines(s);

    assert(s.get_present() == turn_t(1, true));
    assert(data.total == 6);
    assert(data.active == 5);
    assert(data.inactive == 1);
    assert(data.mandatory == 2);
    assert(data.optional == 2);
    assert(data.unplayable == 2);
    assert(data.playable == 4);
    assert(data.friendly_created == 1);
    assert(data.hostile_created == 3);
    assert(data.timeline_advantage == 2);
    assert(data.active_timeline_allowance == 3);
    assert(data.friendly_active_created == 1);
    assert(data.hostile_active_created == 2);
}

void test_standard_move_space_volume()
{
    const state s(*pgnparser(R"(
[Board "Standard - Turn Zero"]
)").parse_game());
    const move_count_data data = count_move_space(s);
    const float expected = std::log(21.0f); // null coordinate + 20 moves

    assert(std::abs(data.log_universe_volume - expected) < 1e-6f);
    assert(std::abs(data.log_non_new_volume - expected) < 1e-6f);
}

void test_branching_move_space_volume()
{
    const state s(*pgnparser(R"(
[Board "Standard - Turn Zero"]

1. Nf3
)").parse_game());
    const move_count_data data = count_move_space(s);

    assert(std::isfinite(data.log_universe_volume));
    assert(std::isfinite(data.log_non_new_volume));
    assert(data.log_non_new_volume >= 0.0f);
    assert(data.log_universe_volume > data.log_non_new_volume);
}

void test_t_exposure_and_blocker()
{
    multiverse_odd open_multiverse({
        {0, 1, false, black_king_a1},
        {0, 1, true,  empty_board},
        {0, 2, false, "8/8/8/8/8/8/8/1k6"},
    });
    const royal_safety_data open = count_royal_safety(state(open_multiverse));
    assert(open.friendly_exposure[royal_safety_data::T_PLUS] == 2);
    assert(open.friendly_exposure[royal_safety_data::T_PLUS_HISTORICAL] == 1);

    multiverse_odd blocked_multiverse({
        {0, 1, false, black_king_a1},
        {0, 1, true,  empty_board},
        {0, 2, false, "8/8/8/8/8/8/8/Pk6"},
    });
    const royal_safety_data blocked = count_royal_safety(state(blocked_multiverse));
    assert(blocked.friendly_exposure[royal_safety_data::T_PLUS] == 1);
    assert(blocked.friendly_exposure[royal_safety_data::T_PLUS_HISTORICAL] == 0);
}

void test_l_exposure_is_directional_and_uses_all_boards()
{
    multiverse_odd multiverse({
        {0, 1, false, black_king_a1},
        {1, 1, false, empty_board},
    });
    const royal_safety_data data = count_royal_safety(state(multiverse));
    assert(data.friendly_exposure[royal_safety_data::L_PLUS] == 2);
    assert(data.friendly_exposure[royal_safety_data::L_MINUS] == 1);
}

void test_l_exposure_flips_for_black_to_move()
{
    multiverse_odd multiverse({
        {0, 1, true, white_king_a1},
        {1, 1, true, empty_board},
    });
    const royal_safety_data data = count_royal_safety(state(multiverse));

    // In raw timeline coordinates the exposure extends toward L+, but Black's
    // feature frame reverses L so that it matches White's perspective.
    assert(data.friendly_exposure[royal_safety_data::L_PLUS] == 1);
    assert(data.friendly_exposure[royal_safety_data::L_MINUS] == 2);

    multiverse_odd diagonal_multiverse({
        {0, 1, true, white_king_a1},
        {1, 2, true, empty_board},
    });
    const royal_safety_data diagonal
        = count_royal_safety(state(diagonal_multiverse));
    assert(diagonal.friendly_exposure[royal_safety_data::L_MINUS_T_PLUS] == 2);
    assert(diagonal.friendly_exposure[royal_safety_data::L_PLUS_T_MINUS] == 1);
}

void test_diagonal_exposure_and_player_perspective()
{
    multiverse_odd diagonal_multiverse({
        {0, 1, false, black_king_a1},
        {1, 2, false, empty_board},
    });
    const royal_safety_data diagonal
        = count_royal_safety(state(diagonal_multiverse));
    assert(diagonal.friendly_exposure[royal_safety_data::L_PLUS_T_PLUS] == 2);
    assert(diagonal.friendly_exposure[royal_safety_data::L_MINUS_T_MINUS] == 1);

    multiverse_odd black_to_move_multiverse({
        {0, 1, true, "8/8/8/8/8/8/8/K7"},
    });
    const royal_safety_data black_to_move
        = count_royal_safety(state(black_to_move_multiverse));
    assert(black_to_move.friendly_exposure[royal_safety_data::T_PLUS] == 1);
    assert(black_to_move.hostile_exposure[royal_safety_data::T_PLUS] == 0);
}

void test_established_and_provisional_vacuum()
{
    multiverse_odd established_multiverse({
        {0, 1, false, black_king_a1},
        {1, 2, false, empty_board},
        {2, 1, false, empty_board},
    });
    const royal_safety_data established
        = count_royal_safety(state(established_multiverse));
    assert(established.friendly_exposure[royal_safety_data::L_PLUS] == 1);

    multiverse_odd provisional_multiverse({
        {0, 1, false, black_king_a1},
        {1, 0, false, empty_board},
        {2, 1, false, empty_board},
    });
    const royal_safety_data provisional
        = count_royal_safety(state(provisional_multiverse));
    assert(provisional.friendly_exposure[royal_safety_data::L_PLUS] == 2);
}

void test_strong_check_uses_distinct_pieces_on_source_board()
{
    multiverse_odd multiverse({
        {0, 1, false, black_kings_a1_b1},
        {0, 1, true,  empty_board},
        {0, 2, false, white_rooks_a1_b1},
    });
    const royal_safety_data data = count_royal_safety(state(multiverse));
    assert(data.friendly_checks == 2);
    assert(data.friendly_strong_checks == 1);
    assert(data.hostile_checks == 0);
    assert(data.hostile_strong_checks == 0);
}

int main()
{
    test_standard_material();
    test_material_difference_after_capture();
    test_initial_odd_multiverse();
    test_white_to_move_with_inactive_timeline();
    test_black_to_move_with_timeline_advantage();
    test_standard_move_space_volume();
    test_branching_move_space_volume();
    test_t_exposure_and_blocker();
    test_l_exposure_is_directional_and_uses_all_boards();
    test_l_exposure_flips_for_black_to_move();
    test_diagonal_exposure_and_player_perspective();
    test_established_and_provisional_vacuum();
    test_strong_check_uses_distinct_pieces_on_source_board();
}
