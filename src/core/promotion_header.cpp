#include "promotion_header.h"
#include <array>
#include <cctype>
#include <stdexcept>

namespace
{
constexpr static std::array promotion_priority{
    QUEEN_W, PRINCESS_W, ROOK_W, BISHOP_W,
    KNIGHT_W, COMMON_KING_W, UNICORN_W, DRAGON_W,
};
} /* anonymous namespace */

promotion_options piece_to_promotion_option(piece_t piece)
{
    switch(piece)
    {
        case QUEEN_W:       return promotion_options::QUEEN;
        case PRINCESS_W:    return promotion_options::PRINCESS;
        case ROOK_W:        return promotion_options::ROOK;
        case BISHOP_W:      return promotion_options::BISHOP;
        case KNIGHT_W:      return promotion_options::KNIGHT;
        case COMMON_KING_W: return promotion_options::COMMON_KING;
        case UNICORN_W:     return promotion_options::UNICORN;
        case DRAGON_W:      return promotion_options::DRAGON;
        default:            return promotion_options::NONE;
    }
}

bool can_promote_to(promotion_options options, piece_t piece)
{
    const promotion_options option = piece_to_promotion_option(piece);
    return option != promotion_options::NONE
        && static_cast<bool>(options & option);
}

piece_t default_promote_to(promotion_options options)
{
    for(piece_t piece : promotion_priority)
    {
        if(can_promote_to(options, piece))
        {
            return piece;
        }
    }
    return NO_PIECE;
}

promotion_options parse_promotions(std::string_view value)
{
    promotion_options result = promotion_options::NONE;
    bool wildcard = false;
    for(char c : value)
    {
        if(c == ',' || std::isspace(static_cast<unsigned char>(c)))
        {
            continue;
        }
        if(c == '*')
        {
            wildcard = true;
            continue;
        }
        const promotion_options option = piece_to_promotion_option(static_cast<piece_t>(c));
        if(option == promotion_options::NONE)
        {
            throw std::runtime_error(
                "parse_promotions(): Invalid promotion piece: " + std::string(1, c));
        }
        result = result | option;
    }
    if(wildcard)
    {
        if(result != promotion_options::NONE)
        {
            throw std::runtime_error(
                "parse_promotions(): '*' cannot be combined with promotion pieces");
        }
        return promotion_options::ALL;
    }
    return result;
}

std::string format_promotions(promotion_options options)
{
    if(options == promotion_options::NONE)
    {
        return "";
    }
    if(options == promotion_options::ALL)
    {
        return "*";
    }
    std::string result;
    for(piece_t piece : promotion_priority)
    {
        if(can_promote_to(options, piece))
        {
            result += static_cast<char>(piece);
        }
    }
    return result;
}
