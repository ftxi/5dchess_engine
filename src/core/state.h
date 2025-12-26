#ifndef STATE_H
#define STATE_H

#include <memory>
#include <string>
#include <set>
#include <optional>
#include <tuple>
#include <iostream>
#include "multiverse.h"
#include "action.h"
#include "generator.h"
#include "ast.h"

class state
{
    std::unique_ptr<multiverse> m;
    /*
     `present` is in L,T coordinate (i.e. not u,v corrdinated).
     These numbers can be inherited from copy-construction; thus they are not necessarily equal to `m.get_present()`.
    */
    int present;
    bool player;
    
    template<bool C>
    std::vector<vec4> gen_movable_pieces_impl(std::vector<int> lines) const;
    
    /*
     find_check_impl<C>(lines)
     For all boards on the end of timelines specified in `lines` with color `C`,
     test if one of piece on that board with color `C` can capture an enermy royal piece.
     */
    template<bool C>
    generator<full_move> find_checks_impl(std::vector<int> lines) const;

public:
    // structures and helpers for in-place apply/undo
    struct apply_log
    {
        // appended boards: pair<line, count>
        std::vector<std::pair<int,int>> appended;
        // inserted lines (branching moves)
        std::vector<int> inserted_lines;
        // present/player before the move(s)
        int old_present;
        bool old_player;
    };

    struct submit_log
    {
        int old_present;
        bool old_player;
    };

    // apply a single move in-place and return an apply_log to undo it later
    apply_log apply_move_inplace(full_move fm, piece_t promote_to = QUEEN_W);
    void undo_apply_move(const apply_log &log);
    // apply a sequence of moves in-place and collect logs in the same order
    void apply_moves_inplace(const moveseq &mvs, std::vector<apply_log> &logs);
    void undo_moves_inplace(const std::vector<apply_log> &logs);
    // submit with record (to be restored by undo_submit)
    submit_log submit_with_record();
    void undo_submit(const submit_log &rec);
    state(multiverse &mtv) noexcept;
    state(const pgnparser_ast::game &g);
    virtual ~state() = default;
    
    // standard copy-constructors
    state(const state& other)
    : m{other.m->clone()}, present{other.present}, player{other.player} {}
    state(state&&) noexcept = default;
    state& operator=(state other) noexcept {
        swap(*this, other);
        return *this;
    }
    friend void swap(state& a, state& b) noexcept {
        std::swap(a.m, b.m);
        std::swap(a.present, b.present);
        std::swap(a.player, b.player);
    }


    /*
     can_apply: Check if the move can be applied to the current state. If yes, return the new state after applying the move; otherwise return std::nullopt.
     Note that this function is different from `apply_move` in that it does not change the current state as a side effect.
    */
    std::optional<state> can_apply(full_move fm, piece_t promote_to = QUEEN_W) const;
    std::optional<state> can_apply(const action &act) const;
    std::optional<state> can_submit() const;
    
    /*
     apply_move: Apply move to the current state as a side effect. Return true if it is successfull.
     Parameter `UNSAFE=true`: unsafe mode, does not check whether the pending move is pseudolegal. If it is indeed not pseudolegal, the outcome may be unexpected.
     */
    template<bool UNSAFE = false>
    bool apply_move(full_move fm, piece_t promote_to = QUEEN_W);
    template<bool UNSAFE = false>
    bool submit();
    
    /*
     phantom: state used for deciding whether the current is a checkmate or stalemate
     */
    state phantom() const;

    /*
     new_line(): return the index of a new line to be created by this->player.
    */
    int new_line() const;
    
    /*
     get_timeline_status() returns `std::make_tuple(mandatory_timelines, optional_timelines, unplayable_timelines)`
     where:
     mandatory_timelines are the timelines that current player must make a move on it
     optional_timelines are the timelines that current player can choose to play or not
     unplayable_timelines are the timelines that current player can't place a move on
     */
    std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> get_timeline_status() const;
    std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> get_timeline_status(int present_t, bool present_c) const;
    
    /*
     find_checks(): Test if that player with color `c` is able to capture an enermy royal piece.
     */
    generator<full_move> find_checks(bool c) const;
    
    std::vector<vec4> gen_movable_pieces() const;
    std::vector<vec4> get_movable_pieces(std::vector<int> lines) const;
    
    
    /*
    pretty_move<FLAGS>(fm, c):
    Return a pretty string representation of the move `fm` from the perspective of player with color `c`.
    (This is the inverse of parse_move())
    FLAGS is a bitmask that controls what information to show.
    */
    constexpr static uint16_t SHOW_NOTHING = 0;
    constexpr static uint16_t SHOW_RELATIVE = 1 << 0;
    constexpr static uint16_t SHOW_PAWN = 1 << 1;
    constexpr static uint16_t SHOW_CAPTURE = 1 << 2;
    constexpr static uint16_t SHOW_PROMOTION = 1 << 3;
    constexpr static uint16_t SHOW_MATE = 1 << 4;
    constexpr static uint16_t SHOW_LCOMMENT = 1 << 5;
    constexpr static uint16_t SHOW_ALL = SHOW_RELATIVE | SHOW_PAWN | SHOW_CAPTURE | SHOW_PROMOTION | SHOW_MATE | SHOW_LCOMMENT;
    template<uint16_t FLAGS=SHOW_CAPTURE | SHOW_PROMOTION>
    std::string pretty_move(full_move fm, piece_t promote_to=QUEEN_W) const;
    template<uint16_t FLAGS=SHOW_CAPTURE | SHOW_PROMOTION>
    std::string pretty_action(action act) const;

    // wrappers for low-level functions
    std::pair<int, int> get_board_size() const;
    turn_t get_present() const;
    turn_t apparent_present() const;
    std::pair<int, int> get_initial_lines_range() const;
    std::pair<int, int> get_lines_range() const;
    std::pair<int, int> get_active_range() const;
    turn_t get_timeline_start(int l) const;
    turn_t get_timeline_end(int l) const;
    piece_t get_piece(vec4 p, bool color) const;
    std::shared_ptr<board> get_board(int l, int t, bool c) const;
    std::vector<std::tuple<int,int,bool,std::string>> get_boards() const;
    generator<vec4> gen_piece_move(vec4 p) const;
    generator<vec4> gen_piece_move(vec4 p, bool c) const;
    std::string to_string() const;
    std::string show_fen() const;
    
    /*
    parse_move: Given a state `s` and a move in string format `move`, try to parse the move and match it to a unique full_move in the context of state `s`.
    - If successful, return a tuple with first index set to the matched full_move and second index set to the promotion piece if any.
    - If failed, return a tuple with first two indices set to nullopt and the third indices containing all possible matching full_moves. (.size()>1 ~> ambiguous; .size()==0 ~> cannot parse/no match)
    */
    using parse_pgn_res = std::tuple<std::optional<full_move>, std::optional<piece_t>, std::vector<full_move>>;
    parse_pgn_res parse_move(const pgnparser_ast::move &move) const;
    parse_pgn_res parse_move(const std::string &move) const;
};

#include "state.inl"

#endif //STATE_H
