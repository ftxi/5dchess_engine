#ifndef CHECK_POSITION_H
#define CHECK_POSITION_H

#include "state.h"
#include <concepts>
#include <optional>
#include <utility>
#include <span>

template<class Emit>
concept check_emitter = requires(Emit& callback, full_move move) {
    { callback(std::move(move)) } -> std::same_as<bool>;
};

/* Borrowed view for finding royal captures. Timeline metadata is copied from
   the base state; board data is borrowed. The state and any added boards must
   outlive the view and remain unchanged. Moves are neither applied nor validated. */
class check_position {
    struct line_view {
        bool exists = false;
        bool in_base = false;
        turn_t start{};
        turn_t end{};
        turn_t added_start{};
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

    /* Scan endpoints of color C. An empty sources span selects all timelines.
       emit receives each royal capture and returns true to stop. The return
       value reports whether emit stopped the scan, not whether any capture exists. */
    template<bool C, check_emitter Emit>
    bool scan(bool include_physical, Emit&& emit,
              std::span<const int> sources = {},
              bool include_superphysical = true) const;

public:
    explicit check_position(const state& s);
    /* Reserve [l_min, l_max] for existing and future timelines. The range must
       contain every timeline in s. */
    check_position(const state& s, int l_min, int l_max);

    /* View s.phantom() without constructing it. For endpoints matching s's
       stored player, reuse the endpoint board on the following turn. */
    static check_position for_phantom(const state& s);

    /* Prepare for candidate checks. Repeat opposite-color endpoint boards on
       the moving player's turn and reserve s.new_line() for a possible branch.
       Supply each move's result boards to gives_check(). */
    static check_position for_move_candidates(const state& s);

    /* Borrowed result board first visible at (l,t,c). gives_check() also uses
       it at next_turn({t,c}). */
    struct board_overlay {
        int l;
        int t;
        bool c;
        const board* value;
    };
    /* Historical means a superphysical capture before the target's endpoint. */
    enum class check_kind { physical, superphysical, historical };

    /* Test for royal captures by attacker from the overlaid timelines. Supply
       the changed departure board for a superphysical move. Restore the view
       afterward. Overlays must have non-null boards and distinct timeline indices. */
    template<check_kind Kind>
    bool gives_check(board_overlay move_overlay, bool attacker,
                     std::optional<board_overlay> departure_overlay = std::nullopt);

    /* Extend a timeline by one turn, or start a reserved empty one. The borrowed
       board stays in this view; each timeline accepts at most one added board. */
    void add_board(int l, turn_t at, const board& b);

    /* Return nullptr when the turn is absent from this view. */
    const board* get_board_ptr(int l, int t, bool c) const;

    piece_t get_piece(vec4 p, bool c) const;

    /* include_physical=false excludes captures on the same board. */
    std::optional<full_move> first_check(bool attacker, bool include_physical = true) const;

    std::vector<full_move> checks(bool attacker, bool include_physical = true) const;
};

#endif
