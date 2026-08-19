#ifndef LINEAR_TRAINING_H
#define LINEAR_TRAINING_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <vector>

#include "linear.h"

/*
 A supervised example for the bounded linear model.

 `target` is always in [-1, 1] from the perspective of the player to move in
 `features`.  Use from_position() when the label is White-centric; it performs
 the required sign conversion for Black-to-move positions.
 */
struct linear_training_sample
{
    linear_engine::feature_vector_t features{};
    float target = 0.0f;
    float weight = 1.0f;

    static linear_training_sample from_position(
        const state &position,
        float white_target,
        float sample_weight = 1.0f);
};

struct linear_training_metrics
{
    // 0.5 * weighted MSE + 0.5 * l2 * ||non-bias weights||^2.
    double objective_loss = 0.0;
    double mean_squared_error = 0.0;
    double mean_absolute_error = 0.0;
    double total_sample_weight = 0.0;
    std::size_t sample_count = 0;
};

struct linear_loss_gradient
{
    linear_training_metrics metrics;
    std::array<double, linear_engine::features_count> gradient{};
};

struct linear_training_options
{
    std::size_t epochs = 100;
    std::size_t batch_size = 64;
    double learning_rate = 0.01;
    double l2 = 0.0001;
    double adam_beta1 = 0.9;
    double adam_beta2 = 0.999;
    double adam_epsilon = 1e-8;
    bool shuffle = true;
    std::uint32_t seed = 0;

    // Early stopping is enabled only when a validation set is supplied and
    // patience is nonzero.  The best validation checkpoint is restored by
    // default even when early stopping does not fire.
    std::size_t early_stopping_patience = 0;
    double early_stopping_min_delta = 0.0;
    bool restore_best_weights = true;
};

struct linear_training_epoch
{
    std::size_t epoch = 0;
    linear_training_metrics training;
    std::optional<linear_training_metrics> validation;
};

struct linear_training_report
{
    linear_training_metrics initial_training;
    linear_training_metrics final_training;
    std::optional<linear_training_metrics> initial_validation;
    std::optional<linear_training_metrics> final_validation;
    std::vector<linear_training_epoch> history;
    std::size_t best_epoch = 0;
    bool stopped_early = false;
};

class linear_trainer
{
    linear_training_options options;

public:
    explicit linear_trainer(linear_training_options options = {});

    static linear_loss_gradient loss_and_gradient(
        std::span<const linear_training_sample> samples,
        const linear_engine::weight_vector_t &weights,
        double l2 = 0.0);

    static linear_training_metrics evaluate(
        std::span<const linear_training_sample> samples,
        const linear_engine::weight_vector_t &weights,
        double l2 = 0.0);

    linear_training_report fit(
        std::span<const linear_training_sample> training_samples,
        linear_engine::weight_vector_t &weights,
        std::span<const linear_training_sample> validation_samples = {}) const;
};

// Stable, schema-versioned text formats for checkpoints and precomputed data.
void write_linear_weights(
    std::ostream &output,
    const linear_engine::weight_vector_t &weights);
linear_engine::weight_vector_t read_linear_weights(std::istream &input);

void write_linear_training_samples(
    std::ostream &output,
    std::span<const linear_training_sample> samples);
std::vector<linear_training_sample> read_linear_training_samples(
    std::istream &input);

#endif /* LINEAR_TRAINING_H */
