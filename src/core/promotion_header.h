#ifndef PROMOTION_HEADER_H
#define PROMOTION_HEADER_H

#include <cstdint>
#include <string>
#include <string_view>
#include "piece.h"
#include "utils.h"

enum class promotion_options : uint8_t
{
    NONE        = 0,
    QUEEN       = 1 << 0,
    ROOK        = 1 << 1,
    BISHOP      = 1 << 2,
    KNIGHT      = 1 << 3,
    UNICORN     = 1 << 4,
    DRAGON      = 1 << 5,
    PRINCESS    = 1 << 6,
    COMMON_KING = 1 << 7,
    ALL = QUEEN | ROOK | BISHOP | KNIGHT | UNICORN | DRAGON
        | PRINCESS | COMMON_KING,
};

template <>
inline constexpr bool enable_bitmask_operators<promotion_options> = true;

static_assert(sizeof(promotion_options) == 1);

promotion_options piece_to_promotion_option(piece_t piece);
bool can_promote_to(promotion_options options, piece_t piece);
piece_t default_promote_to(promotion_options options);

promotion_options parse_promotions(std::string_view value);
std::string format_promotions(promotion_options options);

#endif /* PROMOTION_HEADER_H */
