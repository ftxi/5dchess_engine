#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstddef>
#include <type_traits>

#include "linear.h"
#include "multiverse_variants.h"
#include "pgnparser.h"

namespace
{

static_assert(std::is_base_of_v<linear_mcts_engine, linear_engine>);
static_assert(std::is_base_of_v<
    weighted_linear_mcts_engine, linear_weighted_engine>);

struct observer {};

state standard_position()
{
    return state(*pgnparser(R"(
[Board "Standard - Turn Zero"]
)").parse_game());
}

void test_feature_layout()
{
    static_assert(linear_cutoff_evaluation::features_count == 64);
    const auto features = linear_cutoff_evaluation::extract_features(standard_position());

    assert(features[linear_cutoff_evaluation::bias_offset] == 1.0f);
    const std::array<float, linear_cutoff_evaluation::material_feature_count> expected_sum{
        16.0f, 4.0f, 6.0f, 6.0f, 2.0f, 2.0f, 2.0f, 2.0f
    };
    for(std::size_t i = 0; i < expected_sum.size(); ++i)
    {
        assert(features[linear_cutoff_evaluation::mandatory_material_sum_offset + i]
               == expected_sum[i]);
        assert(features[linear_cutoff_evaluation::mandatory_material_diff_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::optional_material_sum_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::optional_material_diff_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::unplayable_material_sum_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::unplayable_material_diff_offset + i]
               == 0.0f);
    }

    const std::array<float, linear_cutoff_evaluation::timeline_feature_count>
        expected_timelines{
            1.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f
        };
    for(std::size_t i = 0; i < expected_timelines.size(); ++i)
    {
        assert(features[linear_cutoff_evaluation::timeline_offset + i]
               == expected_timelines[i]);
    }

    const float expected_move_space = std::log(21.0f);
    assert(std::abs(
        features[linear_cutoff_evaluation::log_universe_volume_offset]
        - expected_move_space) < 1e-6f);
    assert(std::abs(
        features[linear_cutoff_evaluation::log_non_new_volume_offset]
        - expected_move_space) < 1e-6f);
}

void test_weights_and_perspective()
{
    linear_cutoff_evaluation::weight_vector_t weights{};
    weights[linear_cutoff_evaluation::bias_offset] = 1.0f;
    linear_cutoff_evaluation evaluation(weights);

    const state white_to_move = standard_position();
    assert(std::abs(evaluation.evaluate(white_to_move) - std::tanh(1.0f)) < 1e-6f);
    assert(evaluation.get_weights() == weights);

    const state black_to_move(*pgnparser(R"(
[Board "Standard - Turn Zero"]

1. e4
)").parse_game());
    assert(std::abs(evaluation.evaluate(black_to_move) + std::tanh(1.0f)) < 1e-6f);
}

void test_builtin_weight_profiles()
{
    const auto hand_written = linear_cutoff_evaluation::default_weights();
    const auto trained = linear_cutoff_evaluation::trained_weights();

    assert(hand_written.size() == 64);
    assert(trained.size() == 64);
    assert(hand_written != trained);
    assert(hand_written[linear_cutoff_evaluation::mandatory_material_diff_offset] == 0.05f);
    assert(hand_written[linear_cutoff_evaluation::timeline_advantage_offset] == 0.25f);
    assert(hand_written[linear_cutoff_evaluation::log_universe_volume_offset] == 0.04f);
    assert(hand_written[linear_cutoff_evaluation::log_non_new_volume_offset] == 0.04f);
    assert(trained[linear_cutoff_evaluation::bias_offset] == -0.00123027945f);
    assert(trained[linear_cutoff_evaluation::log_universe_volume_offset] == 0.0140336938f);
    assert(trained[linear_cutoff_evaluation::log_non_new_volume_offset] == 0.0197210461f);

    assert(linear_cutoff_evaluation(hand_written).get_weights() == hand_written);
}

void test_rollout_policy_preserves_input()
{
    state original = standard_position();
    const turn_t initial_turn = original.get_present();
    observer obs;
    rollout_default_policy policy({}, {}, 1, 7);
    const auto result = policy.evaluate(original, {}, obs);
    assert(result);
    assert(result->data.end == rollout_details::termination::ACTION_LIMIT);
    assert(result->data.num_actions == 1);
    assert(original.get_present() == initial_turn);
}

void test_stalemate_rollout_termination()
{
    multiverse_odd multiverse({
        {0, 1, true, "k7/2Q5/2K5/8/8/8/8/8"}
    });
    observer obs;
    rollout_default_policy policy({}, {}, 1, 1);
    const auto result = policy.evaluate(state(multiverse), {}, obs);
    assert(result);
    assert(result->data.end == rollout_details::termination::STALEMATE);
    assert(result->data.num_actions == 0);
}

void test_winner_rollout_termination()
{
    multiverse_odd white_win_multiverse({
        {0, 1, true, "k7/1Q6/2K5/8/8/8/8/8"}
    });
    observer obs;
    rollout_default_policy policy({}, {}, 1, 1);
    const auto white_win = policy.evaluate(state(white_win_multiverse), {}, obs);
    assert(white_win);
    assert(white_win->data.end == rollout_details::termination::WHITE_WINS);
    assert(white_win->data.num_actions == 0);

    multiverse_odd black_win_multiverse({
        {0, 1, false, "K7/1q6/2k5/8/8/8/8/8"}
    });
    const auto black_win = policy.evaluate(state(black_win_multiverse), {}, obs);
    assert(black_win);
    assert(black_win->data.end == rollout_details::termination::BLACK_WINS);
    assert(black_win->data.num_actions == 0);
}

void test_policy_evaluates_final_rollout_state()
{
    linear_cutoff_evaluation::weight_vector_t weights{};
    weights[linear_cutoff_evaluation::bias_offset] = 1.0f;
    observer obs;
    linear_default_policy policy(
        random_action_selection{},
        linear_cutoff_evaluation{weights},
        1,
        11);
    const auto result = policy.evaluate(standard_position(), {}, obs);
    assert(result);
    assert(result->data.end == rollout_details::termination::ACTION_LIMIT);
    assert(std::abs(result->score + std::tanh(1.0f)) < 1e-6f);
}

} /* anonymous namespace */

int main()
{
    test_feature_layout();
    test_weights_and_perspective();
    test_builtin_weight_profiles();
    test_rollout_policy_preserves_input();
    test_stalemate_rollout_termination();
    test_winner_rollout_termination();
    test_policy_evaluates_final_rollout_state();
    return 0;
}
