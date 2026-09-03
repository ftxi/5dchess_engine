#undef NDEBUG
#include <cassert>
#include <cmath>
#include <string>

#include "pgnparser.h"
#include "move_info_evaluation.h"

namespace
{

bool close(float a, float b)
{
    return std::abs(a - b) < 1e-5f;
}

state position(std::string board)
{
    return state(*pgnparser(
        "[Size \"4x4\"]\n[Board \"custom\"]\n[" + board + ":0:1:w]\n")
        .parse_game());
}

void test_default_weights()
{
    const auto &weights = default_move_info_weights.values;
    assert(weights[move_info_weights::DANGEROUS_KING_MOVE] == -50000.0f);
    assert(weights[move_info_weights::NORMAL_KING_MOVE] == -1200.0f);
    assert(weights[move_info_weights::KING_BRANCH] == -8000.0f);
    assert(weights[move_info_weights::QUEEN_CAPTURE] == 1584.0f);
    assert(weights[move_info_weights::PRINCESS_CAPTURE] == 880.0f);
    assert(weights[move_info_weights::DRAGON_UNICORN_CAPTURE] == 495.0f);
    assert(weights[move_info_weights::BISHOP_KNIGHT_CAPTURE] == 385.0f);
    assert(weights[move_info_weights::ROOK_COMMON_KING_CAPTURE] == 330.0f);
    assert(weights[move_info_weights::BRAWN_CAPTURE] == 132.0f);
    assert(weights[move_info_weights::PAWN_CAPTURE] == 110.0f);
    assert(weights[move_info_weights::CHECK] == 1200.0f);
    assert(weights[move_info_weights::SUPERPHYSICAL] == -700.0f);
}

void test_scores_and_temperature()
{
    state::move_info info{
        nullptr,
        vec4(0, 0, 0, 0),
        KING_W,
        QUEEN_B,
        special_move_t::CAPTURE
            | special_move_t::SUPERPHYSICAL
            | special_move_t::BRANCHING,
        check_type_t::SP_CHECK | check_type_t::HISTORICAL_CHECK,
    };

    const auto features = extract_move_info_features(info);
    assert(features[move_info_weights::NORMAL_KING_MOVE] == 1.0f);
    assert(features[move_info_weights::KING_BRANCH] == 1.0f);
    assert(features[move_info_weights::QUEEN_CAPTURE] == 1.0f);
    assert(features[move_info_weights::CHECK] == 1.0f);
    assert(features[move_info_weights::SUPERPHYSICAL] == 1.0f);

    // -1200 king, -8000 king branch, +1540 capture, +1200 check,
    // and -700 time travel.
    assert(close(move_info_score(info, default_move_info_weights), -7116.0f));

    info.special_move |= special_move_t::DANGEROUS_KING_MOVE;
    assert(close(move_info_score(info, default_move_info_weights), -55916.0f));

    assert(close(
        move_info_score_to_weight(0.0f, default_move_info_temperature),
        1.0f));
    assert(close(
        move_info_score_to_weight(1200.0f, default_move_info_temperature),
        std::exp(1.0f)));
    assert(close(
        move_info_score_to_weight(-1200.0f, default_move_info_temperature),
        std::exp(-1.0f)));

    // Extreme scores retain their naturally tiny exponential weights.
    assert(move_info_score_to_weight(-50000.0f, default_move_info_temperature)
           == std::exp(-50000.0f / default_move_info_temperature));
}

void test_custom_weights()
{
    move_info_weights weights{};
    weights.values[move_info_weights::CHECK] = 3.5f;
    state::move_info info{
        nullptr,
        vec4(0, 0, 0, 0),
        ROOK_W,
        NO_PIECE,
        special_move_t::NONE,
        check_type_t::PHYSICAL_CHECK,
    };
    assert(move_info_score(info, weights) == 3.5f);
    assert(move_info_weight(info, weights, 1.0f) == std::exp(3.5f));
}

void test_latent_king_danger()
{
    const auto knight_info = position("3k/nK2/4/4").get_move_info(
        full_move("(0T1)b3c3"));
    assert(static_cast<bool>(
        knight_info.special_move & special_move_t::DANGEROUS_KING_MOVE));

    const auto bishop_info = position("3k/1K2/b3/4").get_move_info(
        full_move("(0T1)b3b2"));
    assert(static_cast<bool>(
        bishop_info.special_move & special_move_t::DANGEROUS_KING_MOVE));

    const auto unicorn_info = position("3k/1K2/4/u3").get_move_info(
        full_move("(0T1)b3b2"));
    assert(static_cast<bool>(
        unicorn_info.special_move & special_move_t::DANGEROUS_KING_MOVE));
}

} /* anonymous namespace */

int main()
{
    test_default_weights();
    test_scores_and_temperature();
    test_custom_weights();
    test_latent_king_danger();
}
