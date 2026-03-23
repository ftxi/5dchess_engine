#include "variants.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include "multiverse.h"
#include "piece.h"
#include "utils.h"

namespace
{
std::pair<int, int> get_board_size_from_headers(const std::map<std::string, std::string>& metadata)
{
    std::string size_str = find_or_default(metadata, std::string("size"), std::string("8x8"));
    auto pos = size_str.find('x');
    if (pos == std::string::npos)
    {
        throw std::runtime_error("get_board_size_from_headers(): Invalid board size format: " + size_str);
    }

    try {
        int size_x = std::stoi(size_str.substr(0, pos));
        int size_y = std::stoi(size_str.substr(pos + 1));
        if(size_x <= 0 || size_y <= 0 || size_x > BOARD_LENGTH || size_y > BOARD_LENGTH)
        {
            throw std::out_of_range("");
        }
        return {size_x, size_y};
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("get_board_size_from_headers(): Expect number in size value: " + size_str);
    } catch (const std::out_of_range&) {
        throw std::runtime_error(
            "get_board_size_from_headers(): Number out of range in size value: " +
            size_str +
            " (max board size allowed: " + std::to_string(BOARD_LENGTH) + ")"
        );
    }
}
} // anonymous namespace

const std::map<std::string, variant_setup_t> default_variants = {
    {
        "Standard",
        {
            8,8,
            {
                std::make_tuple("r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*", pgnparser_ast::NIL, 0, 1, false)
            },
            false
        }
    },
    {
        "Standard - Turn Zero",
        {
            8,8,
            {
                std::make_tuple("r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*", pgnparser_ast::NIL, 0, 0, true),
                std::make_tuple("r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*", pgnparser_ast::NIL, 0, 1, false),
            },
            false
        }
    },
    {
        "Very Small - Open",
        {
            4,4,
            {
                std::make_tuple("nbrk/3p*/P*3/KRBN", pgnparser_ast::NIL, 0, 1, false),
            },
            false
        }
    },
};

variant_setup_t derive_variant_setup(const pgnparser_ast::game& g)
{
    auto& metadata = g.headers;
    auto [size_x, size_y] = get_board_size_from_headers(metadata);

    std::vector<std::tuple<std::string, pgnparser_ast::token_t, int, int, bool>> boards = g.boards;
    std::optional<bool> is_even_timelines;
    auto it = metadata.find("board");
    if(it != metadata.end())
    {
        std::string board_str = it->second;
        if(board_str == "Custom - Even" || board_str == "Even")
        {
            is_even_timelines = true;
        }
        else if(board_str == "Custom - Odd" || board_str == "Odd")
        {
            is_even_timelines = false;
        }
        else if(board_str.starts_with("Custom"))
        {
        }
        else if(boards.empty())
        {
            try {
                const variant_setup_t& variant = default_variants.at(board_str);
                size_x = variant.size_x;
                size_y = variant.size_y;
                boards = variant.boards;
                is_even_timelines = variant.is_even_timelines;
            } catch (const std::out_of_range&) {
                throw std::runtime_error("derive_variant_setup(): Unknown variant: " + board_str);
            }
        }
    }
    if(boards.empty())
    {
        throw std::runtime_error("derive_variant_setup(): Variant is unspecific: no Board header or 5DFEN given");
    }
    if(!is_even_timelines.has_value())
    {
        bool even = false;
        for(const auto& [fen, sign, l, t, c] : boards)
        {
            even |= (sign == pgnparser_ast::POSITIVE && l == 0);
            even |= (sign == pgnparser_ast::NEGATIVE && l == 0);
            if(even)
            {
                break;
            }
        }
        is_even_timelines = even;
    }

    return {size_x, size_y, boards, *is_even_timelines};
}

std::unique_ptr<multiverse> create_multiverse_from_variant_setup(const variant_setup_t& variant_setup)
{
    std::vector<boards_info_t> boards_info(variant_setup.boards.size());
    if(variant_setup.is_even_timelines)
    {
        std::transform(variant_setup.boards.begin(), variant_setup.boards.end(), boards_info.begin(), [](const auto& tup) {
            const auto& [fen, sign, l, t, c] = tup;
            int signed_l = sign == pgnparser_ast::NEGATIVE ? ~l : l;
            return std::make_tuple(signed_l, t, c, fen);
        });
        return std::make_unique<multiverse_even>(boards_info, variant_setup.size_x, variant_setup.size_y);
    }

    std::transform(variant_setup.boards.begin(), variant_setup.boards.end(), boards_info.begin(), [](const auto& tup) {
        const auto& [fen, sign, l, t, c] = tup;
        int sgn = sign == pgnparser_ast::NEGATIVE ? -1 : 1;
        return std::make_tuple(l*sgn, t, c, fen);
    });
    return std::make_unique<multiverse_odd>(boards_info, variant_setup.size_x, variant_setup.size_y);
}
