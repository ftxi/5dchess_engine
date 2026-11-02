#ifndef TURN_SELECTION_H
#define TURN_SELECTION_H

#include <charconv>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

struct turn_selection
{
    std::size_t actions;
    std::string label;
};

inline std::string submitted_turn(std::size_t actions)
{
    if(actions == 0) return "0b";
    const bool black = actions % 2 == 0;
    return std::to_string(black ? actions / 2 : (actions + 1) / 2)
        + (black ? "b" : "w");
}

inline std::optional<turn_selection> parse_turn_selection(
    std::string_view value, std::string &error)
{
    const auto invalid = [&] {
        error = "invalid turn selector '" + std::string(value)
            + "'; expected 0 or a value such as 1w or 10b";
        return std::optional<turn_selection>{};
    };
    if(value == "0" || value == "0b" || value == "0B")
        return turn_selection{0, "0b"};
    if(value.size() < 2) return invalid();
    const char color = value.back();
    if(color != 'w' && color != 'W' && color != 'b' && color != 'B')
        return invalid();
    const std::string_view digits = value.substr(0, value.size() - 1);
    std::size_t number = 0;
    const auto [end, ec]
        = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if(ec != std::errc{} || end != digits.data() + digits.size() || number == 0)
        return invalid();
    const bool black = color == 'b' || color == 'B';
    if(number > std::numeric_limits<std::size_t>::max() / 2)
    {
        error = "turn selector is too large: '" + std::string(value) + "'";
        return std::nullopt;
    }
    const std::size_t actions = number * 2 - (black ? 0 : 1);
    return turn_selection{actions, submitted_turn(actions)};
}

#endif /* TURN_SELECTION_H */
