#ifndef MOVE_INFO_EVALUATION_H
#define MOVE_INFO_EVALUATION_H

#include <array>

#include "state.h"

struct move_info_weights
{
    enum indices
    {
        DANGEROUS_KING_MOVE,
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

    std::array<float, COUNT> values;
};

extern const move_info_weights default_move_info_weights;
constexpr float default_move_info_temperature = 1200.0f;

std::array<float, move_info_weights::COUNT> extract_move_info_features(
    const state::move_info &info);

float move_info_score(
    const state::move_info &info,
    const move_info_weights &weights);

float move_info_score_to_weight(float score, float temperature);

float move_info_weight(
    const state::move_info &info,
    const move_info_weights &weights,
    float temperature);

#endif /* MOVE_INFO_EVALUATION_H */
