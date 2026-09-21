#ifndef CHECK_POSITION_H
#define CHECK_POSITION_H

#include "state.h"
#include <concepts>
#include <utility>

template<class Emit>
concept check_emitter = requires(Emit& callback, full_move move) {
    { callback(std::move(move)) } -> std::same_as<bool>;
};

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

    template<bool C, check_emitter Emit>
    bool scan_physical(vec4 p0, const board& source,
                       bitboard_t friendly, Emit& emit) const;

    template<bool C, check_emitter Emit>
    bool scan_pure_sliders(vec4 p0, const board& source,
                           bitboard_t friendly, Emit& emit) const;

    template<bool C, check_emitter Emit>
    bool scan_compound_sliders(vec4 p0, const board& source,
                               bitboard_t friendly, Emit& emit) const;

    template<bool C, check_emitter Emit>
    bool scan_jumps(vec4 p0, const board& source,
                    bitboard_t friendly, Emit& emit) const;

    // Emit receives each royal capture and returns true to stop scanning,
    // or false to continue. scan returns whether Emit stopped the traversal.
    template<bool C, check_emitter Emit>
    bool scan(bool include_physical, Emit&& emit) const;

public:
    explicit check_position(const state& s);
    check_position(const state& s, int l_min, int l_max);
    // Equivalent board layout to s.phantom(), borrowing every board from s.
    // Uses the stored player, including before submit() on partial actions.
    static check_position for_phantom(const state& s);
    void add_board(int l, turn_t at, const board& b);
    const board* board_at(int l, int t, bool c) const;
    piece_t get_piece(vec4 p, bool c) const;

    std::optional<full_move> first_check(bool attacker, bool include_physical = true) const;
    // Enumerate royal captures from endpoints matching attacker.
    std::vector<full_move> checks(bool attacker, bool include_physical = true) const;
};

#endif
