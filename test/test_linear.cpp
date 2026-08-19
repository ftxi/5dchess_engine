#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "linear.h"
#include "multiverse_variants.h"
#include "pgnparser.h"
#include "rollout.h"

namespace
{

class sink_io final : public io_handler
{
public:
    std::string read_line() override { return {}; }
    void write_line(const std::string &) override {}
    bool is_open() override { return false; }
};

class test_linear_engine final : public linear_engine
{
public:
    using linear_engine::linear_engine;

    default_policy_result policy(state position, std::mt19937 *rng = nullptr)
    {
        return default_policy(std::move(position), {}, rng);
    }
};

state standard_position()
{
    return state(*pgnparser(R"(
[Board "Standard - Turn Zero"]
)").parse_game());
}

void test_feature_layout()
{
    static_assert(linear_engine::features_count == 84);
    const auto features = linear_engine::extract_features(standard_position());

    assert(features[linear_engine::bias_offset] == 1.0f);
    const std::array<float, linear_engine::material_feature_count> expected_sum{
        16.0f, 4.0f, 6.0f, 6.0f, 2.0f, 2.0f, 2.0f, 2.0f
    };
    for(std::size_t i = 0; i < expected_sum.size(); ++i)
    {
        assert(features[linear_engine::mandatory_material_sum_offset + i]
               == expected_sum[i]);
        assert(features[linear_engine::mandatory_material_diff_offset + i]
               == 0.0f);
        assert(features[linear_engine::optional_material_sum_offset + i]
               == 0.0f);
        assert(features[linear_engine::optional_material_diff_offset + i]
               == 0.0f);
        assert(features[linear_engine::unplayable_material_sum_offset + i]
               == 0.0f);
        assert(features[linear_engine::unplayable_material_diff_offset + i]
               == 0.0f);
    }

    const std::array<float, linear_engine::timeline_feature_count>
        expected_timelines{
            1.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f
        };
    for(std::size_t i = 0; i < expected_timelines.size(); ++i)
    {
        assert(features[linear_engine::timeline_offset + i]
               == expected_timelines[i]);
    }

    const float expected_move_volume = std::log(21.0f);
    assert(std::abs(features[linear_engine::log_universe_volume_offset]
                    - expected_move_volume) < 1e-6f);
    assert(std::abs(features[linear_engine::log_non_new_volume_offset]
                    - expected_move_volume) < 1e-6f);
}

void test_weights_and_perspective()
{
    linear_engine::weight_vector_t weights{};
    weights[linear_engine::bias_offset] = 1.0f;
    linear_engine engine(std::make_unique<sink_io>(), std::nullopt, 0, weights);

    const state white_to_move = standard_position();
    assert(std::abs(engine.evaluate(white_to_move) - std::tanh(1.0f)) < 1e-6f);
    assert(engine.get_weights() == weights);

    const state black_to_move(*pgnparser(R"(
[Board "Standard - Turn Zero"]

1. e4
)").parse_game());
    assert(std::abs(engine.evaluate(black_to_move) + std::tanh(1.0f)) < 1e-6f);
}

void test_default_weights()
{
    const auto weights = linear_engine::default_weights();
    assert(weights[linear_engine::mandatory_material_diff_offset
                   + material_data<int>::QUEEN] == 0.45f);
    assert(weights[linear_engine::optional_material_diff_offset
                   + material_data<int>::QUEEN] == 0.225f);
    assert(weights[linear_engine::unplayable_material_diff_offset
                   + material_data<int>::QUEEN] == 0.0675f);
    assert(weights[linear_engine::mandatory_material_diff_offset
                   + material_data<int>::LPAWN] == 0.05f);
    assert(weights[linear_engine::mandatory_material_diff_offset
                   + material_data<int>::LUNICORN] == 0.08f);
    assert(weights[linear_engine::mandatory_material_diff_offset
                   + material_data<int>::LDRAGON] == 0.05f);
    assert(weights[linear_engine::mandatory_material_diff_offset
                   + material_data<int>::ROYAL] == 0.30f);

    const float rook_value = weights[linear_engine::mandatory_material_diff_offset
                                     + material_data<int>::LROOK];
    const float princess_value
        = rook_value
        + weights[linear_engine::mandatory_material_diff_offset
                  + material_data<int>::LBISHOP];
    const float queen_value
        = princess_value
        + weights[linear_engine::mandatory_material_diff_offset
                  + material_data<int>::LUNICORN]
        + weights[linear_engine::mandatory_material_diff_offset
                  + material_data<int>::LDRAGON]
        + weights[linear_engine::mandatory_material_diff_offset
                  + material_data<int>::QUEEN];
    assert(rook_value < princess_value);
    assert(princess_value < queen_value);
    assert(weights[linear_engine::timeline_advantage_offset] == 0.25f);
    assert(weights[linear_engine::log_universe_volume_offset] == 0.04f);
    assert(weights[linear_engine::log_non_new_volume_offset] == 0.04f);
    assert(weights[linear_engine::checks_diff_offset] == 0.25f);
    assert(weights[linear_engine::strong_checks_diff_offset] == 0.40f);

    std::size_t nonzero = 0;
    for(const float weight : weights)
    {
        nonzero += weight != 0.0f;
    }
    assert(nonzero == 37);

    linear_engine engine(std::make_unique<sink_io>(), std::nullopt, 0);
    assert(engine.get_weights() == weights);
    assert(engine.evaluate(standard_position()) > 0.0f);
}

void test_royal_exposure_log_features()
{
    multiverse_odd multiverse({
        {0, 1, false, "8/8/8/8/8/8/8/k7"},
        {0, 1, true,  "8/8/8/8/8/8/8/8"},
        {0, 2, false, "8/8/8/8/8/8/8/1k6"},
    });
    const auto features = linear_engine::extract_features(state(multiverse));
    const std::size_t t_plus = linear_engine::royal_safety_offset
                             + 2 * royal_safety_data::T_PLUS;
    const std::size_t t_history = linear_engine::royal_safety_offset
                                + 2 * royal_safety_data::T_PLUS_HISTORICAL;
    assert(std::abs(features[t_plus] - std::log1p(2.0f)) < 1e-6f);
    assert(std::abs(features[t_plus + 1] - std::log1p(2.0f)) < 1e-6f);
    assert(std::abs(features[t_history] - std::log1p(1.0f)) < 1e-6f);
    assert(std::abs(features[t_history + 1] - std::log1p(1.0f)) < 1e-6f);
}

void test_rollout_value_and_inplace_semantics()
{
    state original = standard_position();
    const turn_t initial_turn = original.get_present();

    std::mt19937 value_rng(7);
    const auto value_winner = rollout(original, 1, {}, &value_rng);
    assert(!value_winner.has_value());
    assert(original.get_present() == initial_turn);

    std::mt19937 inplace_rng(7);
    const auto inplace_winner = rollout_inplace(original, 1, {}, &inplace_rng);
    assert(!inplace_winner.has_value());
    assert(original.get_present() == next_turn(initial_turn));

    state detailed_position = standard_position();
    std::mt19937 detailed_rng(7);
    const rollout_result cutoff = rollout_inplace_detailed(
        detailed_position, 1, {}, &detailed_rng);
    assert(cutoff.termination == rollout_termination::ACTION_LIMIT);
    assert(!cutoff.is_conclusive());
    assert(!cutoff.winner.has_value());
    assert(cutoff.actions == 1);
}

void test_stalemate_is_conclusive()
{
    multiverse_odd multiverse({
        {0, 1, true, "k7/2Q5/2K5/8/8/8/8/8"}
    });
    state position(multiverse);
    assert(position.get_present().second);
    assert(position.get_mate_type() == state::mate_type::STALEMATE);

    const rollout_result stalemate = rollout_detailed(position, 1);
    assert(stalemate.termination == rollout_termination::STALEMATE);
    assert(stalemate.is_conclusive());
    assert(!stalemate.winner.has_value());
    assert(stalemate.actions == 0);
}

void test_policy_evaluates_final_rollout_state()
{
    linear_engine::weight_vector_t weights{};
    weights[linear_engine::bias_offset] = 1.0f;
    test_linear_engine engine(
        std::make_unique<sink_io>(), std::nullopt, 1, weights);

    std::mt19937 rng(11);
    const default_policy_result result = engine.policy(standard_position(), &rng);
    assert(result.termination == rollout_termination::ACTION_LIMIT);
    assert(!result.is_conclusive());
    assert(std::abs(result.score + std::tanh(1.0f)) < 1e-6f);
}

} /* anonymous namespace */

int main()
{
    test_feature_layout();
    test_weights_and_perspective();
    test_default_weights();
    test_royal_exposure_log_features();
    test_rollout_value_and_inplace_semantics();
    test_stalemate_is_conclusive();
    test_policy_evaluates_final_rollout_state();
    return 0;
}
