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
    weights.values[move_info_weights::LATENT_KING_THREAT] = -50000.0f;
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
    const move_info_input &info)
{
    std::array<float, move_info_weights::COUNT> features{};
    const piece_t moved_piece = to_white(piece_name(info.moved_piece));
    const bool king_move = moved_piece == KING_W || moved_piece == COMMON_KING_W;

    if(info.latent_king_threat)
    {
        features[move_info_weights::LATENT_KING_THREAT] = 1.0f;
    }
    else if(king_move)
    {
        features[move_info_weights::NORMAL_KING_MOVE] = 1.0f;
    }

    if(king_move
       && static_cast<bool>(info.move_type & special_move_t::BRANCHING))
    {
        features[move_info_weights::KING_BRANCH] = 1.0f;
    }

    if(info.captured_piece != NO_PIECE)
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

    if(info.checking)
    {
        features[move_info_weights::CHECK] = 1.0f;
    }

    if(static_cast<bool>(info.move_type & special_move_t::SUPERPHYSICAL))
    {
        features[move_info_weights::SUPERPHYSICAL] = 1.0f;
    }

    return features;
}

float move_info_score(
    const move_info_input &info,
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
    const move_info_input &info,
    const move_info_weights &weights,
    float temperature)
{
    return move_info_score_to_weight(
        move_info_score(info, weights), temperature);
}

float hc_move_info_score(const HC_info& info, semimove_feature& features,
                         index_t axis, index_t coordinate,
                         const move_info_weights& weights)
{
    const auto cached = info.get_move_boards(axis, coordinate);
    if (!cached) return 0.0f; // Departures and null coordinates are neutral.
    const full_move& move = cached->move;
    const move_info_input input{
        move.moved_piece(info.s),
        move.captured_piece(info.s),
        move.move_type(info.s),
        weights.values[move_info_weights::CHECK] != 0.0f
            && features.is_check(axis, coordinate),
        weights.values[move_info_weights::LATENT_KING_THREAT] != 0.0f
            && features.has_latent_king_threat(axis, coordinate)
    };
    return move_info_score(input, weights);
}

float hc_move_info_weight(const HC_info& info, semimove_feature& features,
                          index_t axis, index_t coordinate,
                          const move_info_weights& weights, float temperature)
{
    return move_info_score_to_weight(
        hc_move_info_score(info, features, axis, coordinate, weights),
        temperature);
}
