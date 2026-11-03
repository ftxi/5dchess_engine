#include <locale>
#include "pgn_loading.h"
#include "variants.h"
#include "utils.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>

namespace
{
struct invalid_variant {
    pgn_validation_error::error_type_t reason;
    std::string header;
};

std::pair<int, int> get_board_size_from_headers(const pgnparser_ast::header_map &metadata)
{
    const auto size = metadata.find("size");
    const std::string size_str = size == metadata.end() ? "8x8" : size->second;
    auto pos = size_str.find('x');
    if (pos == std::string::npos)
    {
        throw invalid_variant{pgn_validation_error::invalid_size{
            pgn_validation_error::invalid_size::reason::format, BOARD_LENGTH}, "size"};
    }

    // Like stoi, accept leading whitespace/signs and stop at trailing text.
    // Detect conversion errors directly rather than relying on library exceptions.
    const auto dimension = [](const std::string &value) {
        char *end;
        errno = 0;
        const long number = std::strtol(value.c_str(), &end, 10);
        if(end == value.c_str())
            throw invalid_variant{pgn_validation_error::invalid_size{
                pgn_validation_error::invalid_size::reason::number, BOARD_LENGTH}, "size"};
        if(errno == ERANGE || number < INT_MIN || number > INT_MAX)
            throw invalid_variant{pgn_validation_error::invalid_size{
                pgn_validation_error::invalid_size::reason::out_of_range, BOARD_LENGTH}, "size"};
        return static_cast<int>(number);
    };
    const int size_x = dimension(size_str.substr(0, pos));
    const int size_y = dimension(size_str.substr(pos + 1));
    if(size_x <= 0 || size_y <= 0 || size_x > BOARD_LENGTH || size_y > BOARD_LENGTH)
        throw invalid_variant{pgn_validation_error::invalid_size{
            pgn_validation_error::invalid_size::reason::out_of_range, BOARD_LENGTH}, "size"};
    return {size_x, size_y};
}

variant_setup_t resolve_variant_setup(const pgnparser_ast::game &g)
{
    auto &metadata = g.headers;
    auto [size_x, size_y] = get_board_size_from_headers(metadata);
    promotion_options promotions = promotion_options::QUEEN;
    if(auto it = metadata.find("promotions"); it != metadata.end())
    {
        try {
            promotions = parse_promotions(it->second);
        } catch(const invalid_promotions &error) {
            throw invalid_variant{pgn_validation_error::invalid_promotions{error.cause, error.piece}, "promotions"};
        }
    }

    std::vector<std::tuple<std::string, pgnparser_ast::token_t, int, int, bool>> boards = g.boards;
    std::optional<bool> is_even_timelines;
    auto it = metadata.find("board");
    if(it == metadata.end())
        it = metadata.find("variant");
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
            const auto found = default_variants.find(board_str);
            if(found == default_variants.end())
                throw invalid_variant{pgn_validation_error::unknown_variant{}, it->first};
            const auto &variant = found->second;
            size_x = variant.size_x;
            size_y = variant.size_y;
            boards = variant.boards;
            is_even_timelines = variant.is_even_timelines;
        }
    }
    if(auto timeline = metadata.find("timeline"); timeline != metadata.end())
    {
        std::string value = timeline->second;
        std::transform(value.begin(), value.end(), value.begin(), [](char c) {
            return std::tolower(c, std::locale::classic());
        });
        if(value == "even")
            is_even_timelines = true;
        else if(value == "odd")
            is_even_timelines = false;
        else
            throw invalid_variant{pgn_validation_error::invalid_timeline{}, "timeline"};
    }
    if(boards.empty())
    {
        throw invalid_variant{pgn_validation_error::missing_setup{}, ""};
    }
    if(!is_even_timelines.has_value())
    {
        bool even = false;
        for(const auto &[fen, sign, l, t, c] : boards)
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

    return {size_x, size_y, boards, *is_even_timelines, promotions};
}

} /* anonymous namespace */

namespace pgn_loading
{
template<bool UNSAFE>
std::pair<state, std::optional<pgn_validation_error>>
load_initial_position(const pgnparser_ast::game &g)
{
    try {
        const auto setup = resolve_variant_setup(g);
        state initial(create_multiverse_from_variant_setup(setup), setup.promotions);
        if constexpr (!UNSAFE) {
            if(const auto exposure = initial.find_royal_exposure(true)) {
                pgn_validation_error::subject_t subject = pgn_validation_error::description{"initial setup"};
                const auto layout = setup.is_even_timelines ? timeline_layout::even : timeline_layout::odd;
                const int line = exposure->capture.from.l();
                const auto at = initial.get_timeline_end(line);
                if(g.source_text) {
                    for(std::size_t i = 0; i < std::min(g.board_sources.size(), g.boards.size()); ++i) {
                        const auto &[fen, sign, l, t, c] = g.boards[i];
                        const int signed_l = sign == pgnparser_ast::NEGATIVE ? layout.mirror(l) : l;
                        if(signed_l == line && turn{t,c} == at) {
                            subject = pgn_validation_error::source_location{g.source_text, g.board_sources[i].declaration};
                            break;
                        }
                    }
                }
                return {std::move(initial), pgn_validation_error(*exposure, std::move(subject))};
            }
        }
        return {std::move(initial), std::nullopt};
    } catch(const invalid_variant &error) {
        const auto source = g.header_sources.find(error.header);
        if(g.source_text && source != g.header_sources.end())
            throw pgn_validation_error(error.reason,
                pgn_validation_error::source_location{g.source_text, source->second.value});
        const auto header = g.headers.find(error.header);
        throw pgn_validation_error(error.reason, pgn_validation_error::description{
            header == g.headers.end() ? "initial setup" : header->second});
    } catch(const board_fen_error &error) {
        // Construction visits boards in input order. Equal FENs have equal
        // validity, so the first matching entry is the failing declaration.
        if(!g.source_text) throw;
        for(std::size_t i = 0; i < std::min(g.board_sources.size(), g.boards.size()); ++i) {
            if(std::get<0>(g.boards[i]) != error.get_fen()) continue;
            const auto start = g.board_sources[i].fen.start;
            throw board_fen_error(error, g.source_text,
                {start + error.get_start(), start + error.get_end()});
        }
        throw;
    }
}

template std::pair<state, std::optional<pgn_validation_error>>
load_initial_position<false>(const pgnparser_ast::game &g);
template std::pair<state, std::optional<pgn_validation_error>>
load_initial_position<true>(const pgnparser_ast::game &g);
} /* namespace pgn_loading */
