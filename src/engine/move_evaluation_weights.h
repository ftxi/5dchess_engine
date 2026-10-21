#ifndef MOVE_EVALUATION_WEIGHTS_H
#define MOVE_EVALUATION_WEIGHTS_H

#include <array>
#include <cstddef>

#include "piece.h"

// Shared 5D material values for capture rewards and immediate exposure.
// Royal kings have no finite material value; legality handles their safety.
constexpr float capture_piece_value(piece_t piece)
{
    switch(to_white(piece_name(piece)))
    {
        case QUEEN_W:
        case ROYAL_QUEEN_W: return 1440.0f;
        case PRINCESS_W: return 800.0f;
        case DRAGON_W:
        case UNICORN_W: return 450.0f;
        case BISHOP_W:
        case KNIGHT_W: return 350.0f;
        case ROOK_W:
        case COMMON_KING_W: return 300.0f;
        case BRAWN_W: return 120.0f;
        case PAWN_W: return 100.0f;
        default: return 0.0f;
    }
}

struct move_evaluation_weights
{
    // Keep feature indices stable for copying and training.
    enum indices
    {
        LATENT_KING_THREAT,
        NORMAL_KING_MOVE,
        KING_BRANCH,
        QUEEN_CAPTURE,
        PRINCESS_CAPTURE,
        DRAGON_UNICORN_CAPTURE,
        BISHOP_KNIGHT_CAPTURE,
        ROOK_COMMON_KING_CAPTURE,
        BRAWN_CAPTURE,
        PAWN_CAPTURE,
        CHECK,
        SUPERPHYSICAL,
        COUNT
    };

    std::array<float, COUNT> values{};
};


inline constexpr move_evaluation_weights default_move_evaluation_weights = [] {
    move_evaluation_weights weights{};
    weights.values[move_evaluation_weights::LATENT_KING_THREAT] = -50000.0f;
    weights.values[move_evaluation_weights::NORMAL_KING_MOVE] = -1200.0f;
    weights.values[move_evaluation_weights::KING_BRANCH] = -8000.0f;

    constexpr float capture_reward_multiplier = 1.1f;
    weights.values[move_evaluation_weights::QUEEN_CAPTURE]
        = capture_piece_value(QUEEN_W) * capture_reward_multiplier;
    weights.values[move_evaluation_weights::PRINCESS_CAPTURE]
        = capture_piece_value(PRINCESS_W) * capture_reward_multiplier;
    weights.values[move_evaluation_weights::DRAGON_UNICORN_CAPTURE]
        = capture_piece_value(DRAGON_W) * capture_reward_multiplier;
    weights.values[move_evaluation_weights::BISHOP_KNIGHT_CAPTURE]
        = capture_piece_value(BISHOP_W) * capture_reward_multiplier;
    weights.values[move_evaluation_weights::ROOK_COMMON_KING_CAPTURE]
        = capture_piece_value(ROOK_W) * capture_reward_multiplier;
    weights.values[move_evaluation_weights::BRAWN_CAPTURE]
        = capture_piece_value(BRAWN_W) * capture_reward_multiplier;
    weights.values[move_evaluation_weights::PAWN_CAPTURE]
        = capture_piece_value(PAWN_W) * capture_reward_multiplier;

    weights.values[move_evaluation_weights::CHECK] = 1200.0f;
    weights.values[move_evaluation_weights::SUPERPHYSICAL] = -700.0f;
    return weights;
}();

// Non-negative profiles for candidate ordering; they do not affect UCT values.
inline constexpr move_evaluation_weights capture_weights = [] {
    move_evaluation_weights weights{};
    for(std::size_t i = move_evaluation_weights::QUEEN_CAPTURE;
        i <= move_evaluation_weights::PAWN_CAPTURE; ++i)
        weights.values[i] = 480.0f;
    return weights;
}();

inline constexpr move_evaluation_weights capture_check_weights = [] {
    auto weights = capture_weights;
    weights.values[move_evaluation_weights::CHECK] = 300.0f;
    return weights;
}();

#endif /* MOVE_EVALUATION_WEIGHTS_H */
