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
        || magic != "5DNEURAL" || version != 1 || schema != "linear70-v1-mover"
        || inputs != 70 || hidden1 != 32 || hidden2 != 16 || outputs != 1)
        throw std::invalid_argument("invalid neural model header or dimensions");
    neural_model model;
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
