#include "move_info_evaluation.h"

#include <cmath>
#include <numeric>
#include <stdexcept>

static_assert(move_info_weights::COUNT == 12);

namespace
{

consteval move_info_weights make_default_move_info_weights()
{
    move_info_weights weights{};
    weights.values[move_info_weights::DANGEROUS_KING_MOVE] = -50000.0f;
    weights.values[move_info_weights::NORMAL_KING_MOVE] = -1200.0f;
    weights.values[move_info_weights::KING_BRANCH] = -8000.0f;

    constexpr static float capture_reward_multiplier = 1.1f;
    weights.values[move_info_weights::QUEEN_CAPTURE]
        = 1440.0f * capture_reward_multiplier;
    weights.values[move_info_weights::PRINCESS_CAPTURE]
        = 800.0f * capture_reward_multiplier;
    weights.values[move_info_weights::DRAGON_UNICORN_CAPTURE]
        = 450.0f * capture_reward_multiplier;
    weights.values[move_info_weights::BISHOP_KNIGHT_CAPTURE]
        = 350.0f * capture_reward_multiplier;
    weights.values[move_info_weights::ROOK_COMMON_KING_CAPTURE]
        = 300.0f * capture_reward_multiplier;
    weights.values[move_info_weights::BRAWN_CAPTURE]
        = 120.0f * capture_reward_multiplier;
    weights.values[move_info_weights::PAWN_CAPTURE]
        = 100.0f * capture_reward_multiplier;

    weights.values[move_info_weights::CHECK] = 1200.0f;
    weights.values[move_info_weights::SUPERPHYSICAL] = -700.0f;
    return weights;
}

} /* anonymous namespace */

const move_info_weights default_move_info_weights
    = make_default_move_info_weights();

std::array<float, move_info_weights::COUNT> extract_move_info_features(
    const state::move_info &info)
{
    std::array<float, move_info_weights::COUNT> features{};
    const piece_t moved_piece = to_white(piece_name(info.moved_piece));
    const bool king_move = moved_piece == KING_W || moved_piece == COMMON_KING_W;

    if(static_cast<bool>(
           info.special_move & special_move_t::DANGEROUS_KING_MOVE))
    {
        features[move_info_weights::DANGEROUS_KING_MOVE] = 1.0f;
    }
    else if(king_move)
    {
        features[move_info_weights::NORMAL_KING_MOVE] = 1.0f;
    }

    if(king_move
       && static_cast<bool>(info.special_move & special_move_t::BRANCHING))
    {
        features[move_info_weights::KING_BRANCH] = 1.0f;
    }

    if(static_cast<bool>(info.special_move & special_move_t::CAPTURE))
    {
        switch(to_white(piece_name(info.captured_piece)))
        {
            case QUEEN_W:
            case ROYAL_QUEEN_W:
                features[move_info_weights::QUEEN_CAPTURE] = 1.0f;
                break;
            case PRINCESS_W:
                features[move_info_weights::PRINCESS_CAPTURE] = 1.0f;
                break;
            case DRAGON_W:
            case UNICORN_W:
                features[move_info_weights::DRAGON_UNICORN_CAPTURE] = 1.0f;
                break;
            case BISHOP_W:
            case KNIGHT_W:
                features[move_info_weights::BISHOP_KNIGHT_CAPTURE] = 1.0f;
                break;
            case ROOK_W:
            case COMMON_KING_W:
                features[move_info_weights::ROOK_COMMON_KING_CAPTURE] = 1.0f;
                break;
            case BRAWN_W:
                features[move_info_weights::BRAWN_CAPTURE] = 1.0f;
                break;
            case PAWN_W:
                features[move_info_weights::PAWN_CAPTURE] = 1.0f;
                break;
            default:
                break;
        }
    }

    if(static_cast<bool>(info.check_type))
    {
        features[move_info_weights::CHECK] = 1.0f;
    }

    if(static_cast<bool>(info.special_move & special_move_t::SUPERPHYSICAL))
    {
        features[move_info_weights::SUPERPHYSICAL] = 1.0f;
    }

    return features;
}

float move_info_score(
    const state::move_info &info,
    const move_info_weights &weights)
{
    const auto features = extract_move_info_features(info);
    return std::inner_product(
        features.begin(),
        features.end(),
        weights.values.begin(),
        0.0f);
}

float move_info_score_to_weight(float score, float temperature)
{
    if(!(temperature > 0.0f))
    {
        throw std::invalid_argument(
            "rollout weighting temperature must be positive");
    }
    return std::exp(score / temperature);
}

float move_info_weight(
    const state::move_info &info,
    const move_info_weights &weights,
    float temperature)
{
    return move_info_score_to_weight(
        move_info_score(info, weights), temperature);
}
