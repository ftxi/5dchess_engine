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

    float policy(state position, std::mt19937 *rng = nullptr)
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
    static_assert(linear_engine::features_count == 82);
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
}

void test_policy_evaluates_final_rollout_state()
{
    linear_engine::weight_vector_t weights{};
    weights[linear_engine::bias_offset] = 1.0f;
    test_linear_engine engine(
        std::make_unique<sink_io>(), std::nullopt, 1, weights);

    std::mt19937 rng(11);
    const float score = engine.policy(standard_position(), &rng);
    assert(std::abs(score + std::tanh(1.0f)) < 1e-6f);
}

} /* anonymous namespace */

int main()
{
    test_feature_layout();
    test_weights_and_perspective();
    test_royal_exposure_log_features();
    test_rollout_value_and_inplace_semantics();
    test_policy_evaluates_final_rollout_state();
    return 0;
}
