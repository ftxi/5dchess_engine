#ifndef NEURAL_H
#define NEURAL_H

#include <filesystem>
#include "linear.h"

// Text format: 5DNEURAL 1, schema tag, dimensions, means, inverse scales,
// then row-major weights and biases for each affine layer. All numbers float32.
class neural_model
{
public:
    using features_t = linear_cutoff_evaluation::feature_vector_t;
    static constexpr std::size_t input_count = 70;
    static constexpr std::size_t parameter_count = 2817;
    static neural_model load(const std::filesystem::path &path);
    float evaluate_features(const features_t &features) const;

private:
    features_t mean{}, inverse_scale{};
    std::array<float, 70 * 32> weights1{};
    std::array<float, 32> bias1{};
    std::array<float, 32 * 16> weights2{};
    std::array<float, 16> bias2{}, weights3{};
    float bias3 = 0;
};

class neural_leaf_policy
{
    std::shared_ptr<const neural_model> model;
public:
    using result_type = reward_t<rollout_details>;
    explicit neural_leaf_policy(std::shared_ptr<const neural_model> model)
        : model(std::move(model))
    {
        if(!this->model) throw std::invalid_argument("missing neural model");
    }
    template<class Observer>
    std::optional<result_type> evaluate(state position, std::stop_token stop, Observer &)
    {
        move_space_data move_space{};
        auto outcome = evaluate_zero_position(position, stop, &move_space);
        if(!outcome || outcome->data.end != rollout_details::termination::ACTION_LIMIT)
            return outcome;
        if(stop.stop_requested()) return std::nullopt;
        const float mover = model->evaluate_features(
            linear_cutoff_evaluation::extract_features(position, move_space));
        // Backpropagation stores White's value; the model predicts the mover's.
        outcome->score = position.get_present().c() ? -mover : mover;
        return outcome;
    }
};

using neural_pw_mcts_engine = basic_mcts_engine<
    progressive_widening_tree_policy<scored_HC_ordering>, neural_leaf_policy,
    sum_backpropagation, most_visited_selection, mcts_observer>;

class neural_pw_engine final : public neural_pw_mcts_engine
{
public:
    neural_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        std::shared_ptr<const neural_model> model)
        : neural_pw_mcts_engine(neural_leaf_policy{std::move(model)},
            progressive_widening_tree_policy<scored_HC_ordering>{seed, constant, alpha,
                [](const HC_info &info, std::stop_token stop) {
                    return move_evaluator(info, capture_check_weights,
                        capture_ordering::mvvlva_exposure).build_score_table(stop);
                }}, {}, {}, {}, std::move(io)) {}
};

static_assert(neural_model::input_count == linear_cutoff_evaluation::features_count);
static_assert(DefaultPolicy<neural_leaf_policy, mcts_observer>);
#endif /* NEURAL_H */
