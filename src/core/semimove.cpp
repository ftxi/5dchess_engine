#include "semimove.h"
#include "utils.h"
#include "state.h"

#include <sstream>
#include <stdexcept>


std::string semimove::to_string() const
{
    return visit(overloads {
        [](const physical_move &move) { return move.m.to_string(); },
        [](const arriving_move &move) { return move.m.to_string(); },
        [](const departing_move &move) { return std::string("departing ") + move.from.to_string(); },
        [](const null_move &) { return std::string("@"); }
    });
}

std::string semimove::lan(const state &s) const
{
    auto show_square = [&s](vec4 p) -> std::string
    {
        std::ostringstream result;
        result << s.pretty_lt(p.tl());
        result << static_cast<char>('a' + p.x()) << static_cast<char>('1' + p.y());
        return result.str();
    };
    auto show_piece = [&s](vec4 from) -> std::string
    {
        const piece_t piece = to_white(piece_name(s.get_piece(from, s.get_present().second)));
        return std::string(1, static_cast<char>(piece));
    };
    return visit(overloads {
        [&show_square, &show_piece](const physical_move &move) {
            return show_square(move.m.from) + "@" + show_piece(move.m.from)
                + static_cast<char>('a' + move.m.to.x())
                + static_cast<char>('1' + move.m.to.y());
        },
        [&show_square, &show_piece](const arriving_move &move) {
            return show_square(move.m.from) + "@" + show_piece(move.m.from) + ">"
                + show_square(move.m.to);
        },
        [&show_square, &show_piece](const departing_move &move) {
            return show_square(move.from) + ">@" + show_piece(move.from);
        },
        [](const null_move &) {
            return std::string("@");
        }
    });
}

vec4 semimove::hotspot() const
{
    return visit(overloads {
        [](const physical_move &ll) {
            return ll.m.from;
        },
        [](const arriving_move &ll) {
            return ll.m.to;
        },
        [](const departing_move &ll) {
            return ll.from;
        },
        [](const null_move &) -> vec4 {
            throw std::logic_error("a null semimove has no hotspot");
        }
    });
}
