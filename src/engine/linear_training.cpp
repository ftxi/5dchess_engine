#include "linear_training.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

constexpr const char *weights_magic = "5dchess-linear-weights";
constexpr const char *samples_magic = "5dchess-linear-samples";

bool finite(double value)
{
    return std::isfinite(value);
}

void validate_sample(const linear_training_sample &sample)
{
    if(!finite(sample.target) || sample.target < -1.0f || sample.target > 1.0f)
    {
        throw std::invalid_argument("linear target must be finite and in [-1, 1]");
    }
    if(!finite(sample.weight) || sample.weight <= 0.0f)
    {
        throw std::invalid_argument("linear sample weight must be finite and positive");
    }
    if(sample.features[linear_engine::bias_offset] != 1.0f)
    {
        throw std::invalid_argument("linear sample bias feature must equal 1");
    }
    for(const float feature : sample.features)
    {
        if(!finite(feature))
        {
            throw std::invalid_argument("linear features must be finite");
        }
    }
}

void validate_weights(const linear_engine::weight_vector_t &weights)
{
    for(const float weight : weights)
    {
        if(!finite(weight))
        {
            throw std::invalid_argument("linear weights must be finite");
        }
    }
}

void validate_l2(double l2)
{
    if(!finite(l2) || l2 < 0.0)
    {
        throw std::invalid_argument("L2 coefficient must be finite and nonnegative");
    }
}

void validate_options(const linear_training_options &options)
{
    if(options.epochs == 0)
    {
        throw std::invalid_argument("training epochs must be positive");
    }
    if(options.batch_size == 0)
    {
        throw std::invalid_argument("training batch size must be positive");
    }
    if(!finite(options.learning_rate) || options.learning_rate <= 0.0)
    {
        throw std::invalid_argument("learning rate must be finite and positive");
    }
    validate_l2(options.l2);
    if(!finite(options.adam_beta1) || options.adam_beta1 < 0.0
       || options.adam_beta1 >= 1.0
       || !finite(options.adam_beta2) || options.adam_beta2 < 0.0
       || options.adam_beta2 >= 1.0)
    {
        throw std::invalid_argument("Adam beta values must be finite and in [0, 1)");
    }
    if(!finite(options.adam_epsilon) || options.adam_epsilon <= 0.0)
    {
        throw std::invalid_argument("Adam epsilon must be finite and positive");
    }
    if(!finite(options.early_stopping_min_delta)
       || options.early_stopping_min_delta < 0.0)
    {
        throw std::invalid_argument(
            "early-stopping minimum delta must be finite and nonnegative");
    }
}

void require_header(
    std::istream &input,
    const char *expected_magic,
    std::size_t &item_count)
{
    std::string magic;
    std::uint32_t version = 0;
    std::size_t feature_count = 0;
    if(!(input >> magic >> version >> feature_count >> item_count))
    {
        throw std::runtime_error("invalid linear data header");
    }
    if(magic != expected_magic)
    {
        throw std::runtime_error("unexpected linear data format");
    }
    if(version != linear_engine::feature_schema_version)
    {
        throw std::runtime_error("incompatible linear feature schema version");
    }
    if(feature_count != linear_engine::features_count)
    {
        throw std::runtime_error("incompatible linear feature count");
    }
}

void require_end(std::istream &input)
{
    std::string trailing;
    if(input >> trailing)
    {
        throw std::runtime_error("unexpected trailing linear data");
    }
}

} /* anonymous namespace */

linear_training_sample linear_training_sample::from_position(
    const state &position,
    float white_target,
    float sample_weight)
{
    linear_training_sample sample{
        linear_engine::extract_features(position),
        position.get_present().second ? -white_target : white_target,
        sample_weight
    };
    validate_sample(sample);
    return sample;
}

linear_trainer::linear_trainer(linear_training_options options)
    : options(std::move(options))
{
    validate_options(this->options);
}

linear_loss_gradient linear_trainer::loss_and_gradient(
    std::span<const linear_training_sample> samples,
    const linear_engine::weight_vector_t &weights,
    double l2)
{
    if(samples.empty())
    {
        throw std::invalid_argument("linear training data must not be empty");
    }
    validate_weights(weights);
    validate_l2(l2);

    linear_loss_gradient result;
    result.metrics.sample_count = samples.size();
    double weighted_squared_error = 0.0;
    double weighted_absolute_error = 0.0;

    for(const linear_training_sample &sample : samples)
    {
        validate_sample(sample);
        const double prediction = linear_engine::predict_player_score(
            sample.features, weights);
        const double error = prediction - sample.target;
        const double sample_weight = sample.weight;
        result.metrics.total_sample_weight += sample_weight;
        weighted_squared_error += sample_weight * error * error;
        weighted_absolute_error += sample_weight * std::abs(error);
        const double chain = sample_weight * error
                           * (1.0 - prediction * prediction);
        for(std::size_t feature = 0; feature < result.gradient.size(); ++feature)
        {
            result.gradient[feature] += chain * sample.features[feature];
        }
    }

    if(!finite(result.metrics.total_sample_weight)
       || result.metrics.total_sample_weight <= 0.0)
    {
        throw std::invalid_argument("linear sample weights have an invalid total");
    }

    const double inverse_weight = 1.0 / result.metrics.total_sample_weight;
    result.metrics.mean_squared_error = weighted_squared_error * inverse_weight;
    result.metrics.mean_absolute_error = weighted_absolute_error * inverse_weight;
    result.metrics.objective_loss = 0.5 * result.metrics.mean_squared_error;
    for(std::size_t feature = 0; feature < result.gradient.size(); ++feature)
    {
        result.gradient[feature] *= inverse_weight;
        if(feature != linear_engine::bias_offset)
        {
            const double weight = weights[feature];
            result.metrics.objective_loss += 0.5 * l2 * weight * weight;
            result.gradient[feature] += l2 * weight;
        }
    }
    return result;
}

linear_training_metrics linear_trainer::evaluate(
    std::span<const linear_training_sample> samples,
    const linear_engine::weight_vector_t &weights,
    double l2)
{
    return loss_and_gradient(samples, weights, l2).metrics;
}

linear_training_report linear_trainer::fit(
    std::span<const linear_training_sample> training_samples,
    linear_engine::weight_vector_t &weights,
    std::span<const linear_training_sample> validation_samples) const
{
    // Validate the complete inputs before mutating the caller's checkpoint.
    validate_weights(weights);
    for(const linear_training_sample &sample : training_samples)
    {
        validate_sample(sample);
    }
    for(const linear_training_sample &sample : validation_samples)
    {
        validate_sample(sample);
    }
    if(training_samples.empty())
    {
        throw std::invalid_argument("linear training data must not be empty");
    }

    linear_training_report report;
    report.initial_training = evaluate(training_samples, weights, options.l2);
    if(!validation_samples.empty())
    {
        report.initial_validation = evaluate(validation_samples, weights, options.l2);
    }

    std::array<double, linear_engine::features_count> first_moment{};
    std::array<double, linear_engine::features_count> second_moment{};
    double beta1_power = 1.0;
    double beta2_power = 1.0;
    std::vector<std::size_t> order(training_samples.size());
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 random(options.seed);

    linear_engine::weight_vector_t best_weights = weights;
    double best_validation_loss = report.initial_validation
        ? report.initial_validation->objective_loss
        : std::numeric_limits<double>::infinity();
    std::size_t epochs_without_improvement = 0;

    std::vector<linear_training_sample> batch;
    batch.reserve(std::min(options.batch_size, training_samples.size()));
    for(std::size_t epoch = 1; epoch <= options.epochs; ++epoch)
    {
        if(options.shuffle)
        {
            std::shuffle(order.begin(), order.end(), random);
        }
        for(std::size_t begin = 0; begin < order.size(); begin += options.batch_size)
        {
            const std::size_t end = begin + std::min(
                options.batch_size, order.size() - begin);
            batch.clear();
            for(std::size_t offset = begin; offset < end; ++offset)
            {
                batch.push_back(training_samples[order[offset]]);
            }
            // loss_and_gradient() normalizes by the weight inside its span.
            // Rescale the data term so a uniformly sampled mini-batch is an
            // unbiased estimate of the complete weighted objective.  Without
            // this step, sample weights would have no effect at batch size 1.
            const linear_loss_gradient loss = loss_and_gradient(
                batch, weights, 0.0);
            const double batch_scale
                = loss.metrics.total_sample_weight * training_samples.size()
                / (static_cast<double>(batch.size())
                   * report.initial_training.total_sample_weight);
            beta1_power *= options.adam_beta1;
            beta2_power *= options.adam_beta2;
            const double first_correction = 1.0 - beta1_power;
            const double second_correction = 1.0 - beta2_power;
            for(std::size_t feature = 0; feature < weights.size(); ++feature)
            {
                const double gradient = loss.gradient[feature] * batch_scale
                    + (feature == linear_engine::bias_offset
                       ? 0.0 : options.l2 * weights[feature]);
                first_moment[feature]
                    = options.adam_beta1 * first_moment[feature]
                    + (1.0 - options.adam_beta1) * gradient;
                second_moment[feature]
                    = options.adam_beta2 * second_moment[feature]
                    + (1.0 - options.adam_beta2) * gradient * gradient;
                const double corrected_first
                    = first_moment[feature] / first_correction;
                const double corrected_second
                    = second_moment[feature] / second_correction;
                const double updated = static_cast<double>(weights[feature])
                    - options.learning_rate * corrected_first
                    / (std::sqrt(corrected_second) + options.adam_epsilon);
                if(!finite(updated)
                   || std::abs(updated) > std::numeric_limits<float>::max())
                {
                    throw std::runtime_error("linear training produced a non-finite weight");
                }
                weights[feature] = static_cast<float>(updated);
            }
        }

        linear_training_epoch epoch_report;
        epoch_report.epoch = epoch;
        epoch_report.training = evaluate(training_samples, weights, options.l2);
        if(!validation_samples.empty())
        {
            epoch_report.validation = evaluate(validation_samples, weights, options.l2);
            const double validation_loss = epoch_report.validation->objective_loss;
            if(validation_loss
               < best_validation_loss - options.early_stopping_min_delta)
            {
                best_validation_loss = validation_loss;
                best_weights = weights;
                report.best_epoch = epoch;
                epochs_without_improvement = 0;
            }
            else
            {
                ++epochs_without_improvement;
            }
        }
        report.history.push_back(epoch_report);

        if(!validation_samples.empty()
           && options.early_stopping_patience != 0
           && epochs_without_improvement >= options.early_stopping_patience)
        {
            report.stopped_early = true;
            break;
        }
    }

    if(!validation_samples.empty() && options.restore_best_weights)
    {
        weights = best_weights;
    }
    else if(validation_samples.empty())
    {
        report.best_epoch = report.history.size();
    }
    report.final_training = evaluate(training_samples, weights, options.l2);
    if(!validation_samples.empty())
    {
        report.final_validation = evaluate(validation_samples, weights, options.l2);
    }
    return report;
}

void write_linear_weights(
    std::ostream &output,
    const linear_engine::weight_vector_t &weights)
{
    validate_weights(weights);
    output << weights_magic << ' ' << linear_engine::feature_schema_version
           << ' ' << linear_engine::features_count << " 1\n";
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for(std::size_t index = 0; index < weights.size(); ++index)
    {
        output << index << ' ' << weights[index] << '\n';
    }
    if(!output)
    {
        throw std::runtime_error("failed to write linear weights");
    }
}

linear_engine::weight_vector_t read_linear_weights(std::istream &input)
{
    std::size_t checkpoint_count = 0;
    require_header(input, weights_magic, checkpoint_count);
    if(checkpoint_count != 1)
    {
        throw std::runtime_error("linear weight file must contain one checkpoint");
    }
    linear_engine::weight_vector_t weights{};
    for(std::size_t expected = 0; expected < weights.size(); ++expected)
    {
        std::size_t index = 0;
        if(!(input >> index >> weights[expected]) || index != expected)
        {
            throw std::runtime_error("invalid or out-of-order linear weight");
        }
    }
    require_end(input);
    validate_weights(weights);
    return weights;
}

void write_linear_training_samples(
    std::ostream &output,
    std::span<const linear_training_sample> samples)
{
    output << samples_magic << ' ' << linear_engine::feature_schema_version
           << ' ' << linear_engine::features_count << ' ' << samples.size() << '\n';
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for(std::size_t index = 0; index < samples.size(); ++index)
    {
        const linear_training_sample &sample = samples[index];
        validate_sample(sample);
        output << index << ' ' << sample.target << ' ' << sample.weight;
        for(const float feature : sample.features)
        {
            output << ' ' << feature;
        }
        output << '\n';
    }
    if(!output)
    {
        throw std::runtime_error("failed to write linear training samples");
    }
}

std::vector<linear_training_sample> read_linear_training_samples(
    std::istream &input)
{
    std::size_t sample_count = 0;
    require_header(input, samples_magic, sample_count);
    std::vector<linear_training_sample> samples(sample_count);
    for(std::size_t expected = 0; expected < sample_count; ++expected)
    {
        std::size_t index = 0;
        linear_training_sample &sample = samples[expected];
        if(!(input >> index >> sample.target >> sample.weight) || index != expected)
        {
            throw std::runtime_error("invalid or out-of-order linear training sample");
        }
        for(float &feature : sample.features)
        {
            if(!(input >> feature))
            {
                throw std::runtime_error("incomplete linear training sample");
            }
        }
        validate_sample(sample);
    }
    require_end(input);
    return samples;
}
