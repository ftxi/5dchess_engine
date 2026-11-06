#include "neural.h"

#include <cmath>
#include <fstream>
#include <locale>
#include <stdexcept>
#include <string>

neural_model neural_model::load(const std::filesystem::path &path)
{
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    std::string magic, schema;
    int version = 0, inputs = 0, hidden1 = 0, hidden2 = 0, outputs = 0;
    if(!(input >> magic >> version >> schema >> inputs >> hidden1 >> hidden2 >> outputs)
        || magic != "5DNEURAL" || version != 1
        || (schema != "linear70-v1-mover" && schema != solid_schema)
        || inputs != 70 || hidden1 != 32 || hidden2 != 16 || outputs != 1)
        throw std::invalid_argument("invalid neural model header or dimensions");
    neural_model model;
    model.uses_solid_features = schema == solid_schema;
    auto read = [&](auto &values) {
        for(auto &value : values)
            if(!(input >> value) || !std::isfinite(value))
                throw std::invalid_argument("invalid or truncated neural model numbers");
    };
    read(model.mean);
    read(model.inverse_scale);
    for(float value : model.inverse_scale)
        if(value < 0)
            throw std::invalid_argument("negative neural normalization scale");
    read(model.weights1); read(model.bias1);
    read(model.weights2); read(model.bias2);
    read(model.weights3);
    std::array<float, 1> bias;
    read(bias);
    model.bias3 = bias[0];
    std::string extra;
    if(input >> extra) throw std::invalid_argument("trailing neural model data");
    return model;
}

neural_model::features_t neural_model::extract_solid_features(const state &position)
{
    auto [info, space] = HC_info::build_HC(position,
        {.remove_check = true, .prune_optional_moves = true});
    (void)space;
    return extract_solid_features(position, info);
}

neural_model::features_t neural_model::extract_solid_features(
    const state &position, const HC_info &info)
{
    // Preserve the non-volume features and the legacy linear schema.
    auto features = linear_cutoff_evaluation::extract_features(position, {});
    auto log_volume = [&](index_t begin, index_t end) {
        double sum = 0;
        for(index_t axis = begin; axis < end; ++axis)
        {
            const auto size = info.get_universe()[axis].size();
            // log(max(1, volume)): empty and unit candidate factors map to zero.
            // In particular, an empty pruned axis must never produce -infinity.
            if(size == 0) return 0.0f;
            sum += std::log(static_cast<double>(size));
        }
        return static_cast<float>(sum);
    };
    features[log_playable_volume_offset] = log_volume(0, info.first_branch_axis());
    features[log_new_volume_offset] = log_volume(info.first_branch_axis(), info.dimension());
    return features;
}

neural_model::features_t neural_model::extract_features(
    const state &position, const move_space_data &legacy_space) const
{
    if(uses_solid_features) return extract_solid_features(position);
    return linear_cutoff_evaluation::extract_features(position, legacy_space);
}

std::optional<reward_t<rollout_details>> neural_model::evaluate_position(
    state position, std::stop_token stop) const
{
    if(stop.stop_requested()) return std::nullopt;
    std::optional<reward_t<rollout_details>> outcome;
    features_t features;
    if(uses_solid_features)
    {
        auto [info, space] = HC_info::build_HC(position,
            {.remove_check = true, .prune_optional_moves = true});
        if(stop.stop_requested()) return std::nullopt;
        const auto witness = first_position_action(info, std::move(space), stop);
        if(stop.stop_requested()) return std::nullopt;
        if(witness)
            outcome = rollout_cutoff_evaluation{}.cutoff_reward(position, 0, stop, nullptr);
        else
            // Optional pruning is heuristic: only the complete space can prove
            // that no action exists. It may instead supply a fallback witness.
            outcome = evaluate_zero_position(position, stop);
        if(!outcome || outcome->data.end != rollout_details::termination::ACTION_LIMIT)
            return outcome;
        features = extract_solid_features(position, info);
    }
    else
    {
        move_space_data space{};
        outcome = evaluate_zero_position(position, stop, &space);
        if(!outcome || outcome->data.end != rollout_details::termination::ACTION_LIMIT)
            return outcome;
        features = extract_features(position, space);
    }
    if(stop.stop_requested()) return std::nullopt;
    const float mover = evaluate_features(features);
    outcome->score = position.get_present().c() ? -mover : mover;
    return outcome;
}

float neural_model::evaluate_features(const features_t &features) const
{
    features_t normalized;
    for(std::size_t i = 0; i < features.size(); ++i)
    {
        if(!std::isfinite(features[i]))
            throw std::invalid_argument("nonfinite neural input");
        normalized[i] = (features[i] - mean[i]) * inverse_scale[i];
        if(!std::isfinite(normalized[i]))
            throw std::invalid_argument("nonfinite normalized neural input");
    }
    auto affine = [](const auto &input, const auto &weights, const auto &bias) {
        auto output = bias;
        for(std::size_t row = 0; row < output.size(); ++row)
        {
            float value = bias[row];
            for(std::size_t col = 0; col < input.size(); ++col)
                value += weights[row * input.size() + col] * input[col];
            if(!std::isfinite(value))
                throw std::invalid_argument("nonfinite neural activation");
            output[row] = std::max(0.0f, value);
        }
        return output;
    };
    const auto hidden1 = affine(normalized, weights1, bias1);
    const auto hidden2 = affine(hidden1, weights2, bias2);
    float z = bias3;
    for(std::size_t i = 0; i < hidden2.size(); ++i) z += weights3[i] * hidden2[i];
    if(!std::isfinite(z)) throw std::invalid_argument("nonfinite neural output");
    return std::tanh(z);
}
