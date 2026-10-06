#ifndef MOVE_INFO_EVALUATION_H
#define MOVE_INFO_EVALUATION_H

#include <array>

#include "hypercuboid.h"
#include "move_feature.h"

struct move_info_weights
{
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

    std::array<float, COUNT> values;
};

extern const move_info_weights default_move_info_weights;
constexpr float default_move_info_temperature = 1200.0f;

struct move_info_input
{
    piece_t moved_piece;
    piece_t captured_piece;
    special_move_t move_type;
    bool checking;
    bool latent_king_threat;
};

std::array<float, move_info_weights::COUNT> extract_move_info_features(
    const move_info_input &info);

float move_info_score(
    const move_info_input &info,
    const move_info_weights &weights);

float move_info_score_to_weight(float score, float temperature);

float move_info_weight(
    const move_info_input &info,
    const move_info_weights &weights,
    float temperature);

// Scores only features with nonzero weights. The context reuses check metadata.
float hc_move_info_score(const HC_info& info, semimove_feature& features,
                         index_t axis, index_t coordinate,
                         const move_info_weights& weights);

float hc_move_info_weight(const HC_info& info, semimove_feature& features,
                          index_t axis, index_t coordinate,
                          const move_info_weights& weights, float temperature);

#endif /* MOVE_INFO_EVALUATION_H */
