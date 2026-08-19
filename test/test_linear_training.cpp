#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "linear_training.h"
#include "pgnparser.h"

namespace
{

class sink_io final : public io_handler
{
public:
    std::string read_line() override { return {}; }
    void write_line(const std::string &) override {}
    bool is_open() override { return false; }
};

state standard_position()
{
    return state(*pgnparser(R"(
[Board "Standard - Turn Zero"]
)").parse_game());
}

linear_training_sample synthetic_sample(float x, float target, float weight = 1.0f)
{
    linear_training_sample sample;
    sample.features[linear_engine::bias_offset] = 1.0f;
    sample.features[linear_engine::mandatory_material_sum_offset] = x;
    sample.target = target;
    sample.weight = weight;
    return sample;
}

template<typename Function>
void assert_throws(Function function)
{
    bool threw = false;
    try
    {
        function();
    }
    catch(const std::exception &)
    {
        threw = true;
    }
    assert(threw);
}

void test_white_target_perspective_conversion()
{
    const state white_to_move = standard_position();
    const auto white_sample = linear_training_sample::from_position(
        white_to_move, 0.75f, 2.0f);
    assert(white_sample.target == 0.75f);
    assert(white_sample.weight == 2.0f);

    const state black_to_move(*pgnparser(R"(
[Board "Standard - Turn Zero"]

1. e4
)").parse_game());
    const auto black_sample = linear_training_sample::from_position(
        black_to_move, 0.75f);
    assert(black_sample.target == -0.75f);
}

void test_training_prediction_matches_engine_evaluation()
{
    linear_engine::weight_vector_t weights{};
    weights[linear_engine::bias_offset] = 0.3f;
    weights[linear_engine::mandatory_material_sum_offset] = -0.01f;
    linear_engine engine(
        std::make_unique<sink_io>(), std::nullopt, 0, weights);

    const state white_to_move = standard_position();
    const double white_prediction = linear_engine::predict_player_score(
        linear_engine::extract_features(white_to_move), weights);
    assert(std::abs(engine.evaluate(white_to_move) - white_prediction) < 1e-7);

    const state black_to_move(*pgnparser(R"(
[Board "Standard - Turn Zero"]

1. e4
)").parse_game());
    const double black_prediction = linear_engine::predict_player_score(
        linear_engine::extract_features(black_to_move), weights);
    assert(std::abs(engine.evaluate(black_to_move) + black_prediction) < 1e-7);

    weights[linear_engine::bias_offset] = -0.2f;
    engine.set_weights(weights);
    assert(engine.get_weights() == weights);
}

void test_analytic_gradient_against_finite_difference()
{
    const std::vector<linear_training_sample> samples{
        synthetic_sample(-1.5f, -0.6f, 0.5f),
        synthetic_sample(0.25f, 0.2f, 2.0f),
        synthetic_sample(2.0f, 0.8f, 1.25f)
    };
    linear_engine::weight_vector_t weights{};
    weights[linear_engine::bias_offset] = 0.17f;
    weights[linear_engine::mandatory_material_sum_offset] = -0.23f;
    weights[linear_engine::mandatory_material_sum_offset + 1] = 0.31f;
    constexpr double l2 = 0.07;
    const auto analytic = linear_trainer::loss_and_gradient(samples, weights, l2);

    constexpr float epsilon = 0.001f;
    const std::array<std::size_t, 3> checked{
        linear_engine::bias_offset,
        linear_engine::mandatory_material_sum_offset,
        linear_engine::mandatory_material_sum_offset + 1
    };
    for(const std::size_t feature : checked)
    {
        auto plus = weights;
        auto minus = weights;
        plus[feature] += epsilon;
        minus[feature] -= epsilon;
        const double plus_loss
            = linear_trainer::evaluate(samples, plus, l2).objective_loss;
        const double minus_loss
            = linear_trainer::evaluate(samples, minus, l2).objective_loss;
        const double numeric = (plus_loss - minus_loss) / (2.0 * epsilon);
        assert(std::abs(numeric - analytic.gradient[feature]) < 2e-5);
    }

    const auto without_l2 = linear_trainer::loss_and_gradient(samples, weights, 0.0);
    assert(std::abs(analytic.gradient[linear_engine::bias_offset]
                    - without_l2.gradient[linear_engine::bias_offset]) < 1e-12);
    const std::size_t regularized
        = linear_engine::mandatory_material_sum_offset + 1;
    assert(std::abs(
        analytic.gradient[regularized] - without_l2.gradient[regularized]
        - l2 * weights[regularized]) < 1e-12);
}

void test_adam_recovers_synthetic_weights()
{
    std::vector<linear_training_sample> samples;
    for(int step = -20; step <= 20; ++step)
    {
        const float x = static_cast<float>(step) / 10.0f;
        samples.push_back(synthetic_sample(
            x, static_cast<float>(std::tanh(0.25 + 0.6 * x))));
    }

    linear_engine::weight_vector_t weights{};
    linear_training_options options;
    options.epochs = 300;
    options.batch_size = samples.size();
    options.learning_rate = 0.03;
    options.l2 = 0.0;
    options.shuffle = false;
    const linear_training_report report
        = linear_trainer(options).fit(samples, weights);

    assert(report.final_training.mean_squared_error < 1e-10);
    assert(std::abs(weights[linear_engine::bias_offset] - 0.25f) < 2e-5f);
    assert(std::abs(weights[linear_engine::mandatory_material_sum_offset]
                    - 0.6f) < 2e-5f);
    assert(report.best_epoch == options.epochs);
    assert(!report.stopped_early);
}

void test_sample_weights_survive_minibatching()
{
    const std::vector<linear_training_sample> samples{
        synthetic_sample(0.0f, 0.75f, 9.0f),
        synthetic_sample(0.0f, -0.75f, 1.0f)
    };
    linear_engine::weight_vector_t weights{};
    const auto initial_gradient = linear_trainer::loss_and_gradient(
        samples, weights);
    assert(std::abs(initial_gradient.gradient[linear_engine::bias_offset]
                    + 0.6) < 1e-12);

    linear_training_options options;
    options.epochs = 4000;
    options.batch_size = 1;
    options.learning_rate = 0.003;
    options.l2 = 0.0;
    options.seed = 19;
    const auto report = linear_trainer(options).fit(samples, weights);
    const double prediction = linear_engine::predict_player_score(
        samples.front().features, weights);
    // The weighted least-squares optimum is (9*0.75 - 0.75) / 10 = 0.6.
    assert(std::abs(prediction - 0.6) < 0.06);
    assert(report.final_training.mean_squared_error
           < report.initial_training.mean_squared_error);
}

void test_validation_checkpoint_and_early_stopping()
{
    const std::vector<linear_training_sample> samples{
        synthetic_sample(-1.0f, -0.5f),
        synthetic_sample(1.0f, 0.5f)
    };
    linear_engine::weight_vector_t weights{};
    const auto initial = weights;
    linear_training_options options;
    options.epochs = 10;
    options.batch_size = samples.size();
    options.learning_rate = 0.1;
    options.l2 = 0.0;
    options.shuffle = false;
    options.early_stopping_patience = 2;
    options.early_stopping_min_delta = 10.0;

    const linear_training_report report
        = linear_trainer(options).fit(samples, weights, samples);
    assert(report.stopped_early);
    assert(report.history.size() == 2);
    assert(report.best_epoch == 0);
    assert(weights == initial);
    assert(report.final_validation.has_value());
    assert(report.final_validation->objective_loss
           == report.initial_validation->objective_loss);
}

void test_schema_checked_round_trip()
{
    auto weights = linear_engine::default_weights();
    weights[linear_engine::bias_offset] = 0.123456789f;
    std::stringstream weight_stream;
    write_linear_weights(weight_stream, weights);
    assert(read_linear_weights(weight_stream) == weights);

    const std::vector<linear_training_sample> samples{
        synthetic_sample(-2.0f, -0.7f, 0.25f),
        synthetic_sample(3.0f, 0.9f, 4.0f)
    };
    std::stringstream sample_stream;
    write_linear_training_samples(sample_stream, samples);
    const auto restored = read_linear_training_samples(sample_stream);
    assert(restored.size() == samples.size());
    for(std::size_t i = 0; i < samples.size(); ++i)
    {
        assert(restored[i].features == samples[i].features);
        assert(restored[i].target == samples[i].target);
        assert(restored[i].weight == samples[i].weight);
    }

    std::stringstream wrong_version;
    wrong_version << "5dchess-linear-weights 999 "
                  << linear_engine::features_count << " 1\n";
    assert_throws([&] { (void)read_linear_weights(wrong_version); });

    auto invalid = synthetic_sample(0.0f, 1.1f);
    assert_throws([&] {
        (void)linear_trainer::evaluate(
            std::span<const linear_training_sample>(&invalid, 1), weights);
    });
}

} /* anonymous namespace */

int main()
{
    test_white_target_perspective_conversion();
    test_training_prediction_matches_engine_evaluation();
    test_analytic_gradient_against_finite_difference();
    test_adam_recovers_synthetic_weights();
    test_sample_weights_survive_minibatching();
    test_validation_checkpoint_and_early_stopping();
    test_schema_checked_round_trip();
    return 0;
}
