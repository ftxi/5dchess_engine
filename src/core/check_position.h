#ifndef CHECK_POSITION_H
#define CHECK_POSITION_H

#include "state.h"
#include <concepts>
#include <utility>

// Borrowed view of one HC candidate or phantom position. The base and boards must
// outlive the view and remain unchanged. At most one board is added per line.
// The view owns timeline metadata and borrows board storage.
class check_position {
    struct line_view {
        bool exists = false;
        bool in_base = false;
        turn_t start{};
        turn_t end{};
        const board* added = nullptr;
    };
    const state& base;
    int first_line;
    std::vector<line_view> lines;

    // Emit receives each royal capture and returns true to stop scanning,
    // or false to continue. scan returns whether Emit stopped the traversal.
    template<bool C, class Emit>
        requires requires(Emit& callback, full_move move) {
            { callback(std::move(move)) } -> std::same_as<bool>;
        }
    bool scan(bool physical, Emit&& emit) const;

public:
    explicit check_position(const state& s);
    check_position(const state& s, int reserved_min, int reserved_max);
    // Equivalent board layout to s.phantom(), borrowing every board from s.
    // Uses the stored player, including before submit() on partial actions.
    static check_position for_phantom(const state& s);
    void add_board(int l, turn_t at, const board& b);
    const board* board_at(int l, int t, bool c) const;
    piece_t get_piece(vec4 p, bool c) const;

    std::optional<full_move> first_check(bool attacker, bool physical = true) const;
    // Enumerate royal captures from endpoints matching attacker.
    std::vector<full_move> checks(bool attacker, bool physical = true) const;
};

#endif
