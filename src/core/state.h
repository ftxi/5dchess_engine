#ifndef STATE_H
#define STATE_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <iostream>
#include "multiverse.h"
#include "action.h"
#include "promotion_header.h"
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
    promotion_options promotions;
    
    template<bool C>
    std::vector<vec4> gen_movable_pieces_impl(const std::vector<int> &lines) const;
    
    /*
     find_check_impl<C>(lines)
     For all boards on the end of timelines specified in `lines` with color `C`,
     test if one of piece on that board with color `C` can capture an enermy royal piece.
     */
    template<bool C>
    generator<full_move> find_checks_impl(std::vector<int> lines) const;

public:
    state(multiverse &mtv, promotion_options promotions = promotion_options::QUEEN) noexcept;
    state(const pgnparser_ast::game &g);
    virtual ~state() = default;
    
    // standard copy-constructors
    state(const state &other)
    : m{other.m->clone()}, present{other.present}, player{other.player}, promotions{other.promotions} {}
    state(state&&) noexcept = default;
    state &operator=(state other) noexcept {
        swap(*this, other);
        return *this;
    }
    friend void swap(state &a, state &b) noexcept {
        std::swap(a.m, b.m);
        std::swap(a.present, b.present);
        std::swap(a.player, b.player);
        std::swap(a.promotions, b.promotions);
    }


    /*
     can_apply: Check if the move can be applied to the current state. If yes, return the new state after applying the move; otherwise return std::nullopt.
     Note that this function is different from `apply_move` in that it does not change the current state as a side effect.
    */
    std::optional<state> can_apply(full_move fm, piece_t promote_to = NO_PIECE) const;
    std::optional<state> can_apply(const action &act) const;
    std::optional<state> can_submit() const;
    
    /*
     apply_move: Apply move to the current state as a side effect. Return true if it is successfull.
     The full_move overload resolves the configured promotion before applying
     the move in either safety mode. The ext_move overload expects an explicit,
     normalized promotion choice; NO_PIECE means no promotion, not the default.
     UNSAFE=true skips validation and trusts the supplied move's legality.
     */
    template<bool UNSAFE = false>
    bool apply_move(full_move fm);
    template<bool UNSAFE = false>
    bool apply_move(ext_move mv);
    template<bool UNSAFE = false>
    bool submit();
    
    /*
     move_info: given a generated move, apply it and describe the result.
     get_move_info assumes that the move is pseudolegal and applies it in unsafe mode.
     An omitted promotion is resolved against this state; an explicit choice is trusted.
     In a castling move, it is considered a check if either moved piece checks an
     opponent royal piece.
     */
    struct move_info {
        std::unique_ptr<state> new_state;
        vec4 new_pos;
        piece_t moved_piece;
        piece_t captured_piece;
        special_move_t special_move;
        check_type_t check_type;
    };
    move_info get_move_info(full_move fm, piece_t promote_to = NO_PIECE) const;
    
    /*
     phantom: state used for deciding whether the current is a checkmate or stalemate
     */
    state phantom() const;
    // Equivalent to phantom().find_checks(!get_present().second).first(),
    // tested for presence, without cloning the multiverse. Includes physical checks.
    bool has_phantom_check() const;

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
    std::vector<vec4> get_movable_pieces(const std::vector<int> &lines) const;
    
    
    mate_type get_mate_type() const;
    bool is_softmate() const;

private:
    mate_type get_mate_type_impl(bool legal_action_witness) const;

public:

    // wrappers for low-level functions
    std::pair<int, int> get_board_size() const;
    promotion_options get_promotion_options() const { return promotions; }
    std::optional<ext_move> normalize_promotion(ext_move move) const;
    turn_t get_present() const;
    turn_t apparent_present() const;
    std::pair<int, int> get_initial_lines_range() const;
    std::pair<int, int> get_lines_range() const;
    std::pair<int, int> get_active_range() const;
    turn_t get_timeline_start(int l) const;
    turn_t get_timeline_end(int l) const;
    piece_t get_piece(vec4 p, bool color) const;
    std::shared_ptr<board> get_board(int l, int t, bool c) const;
    const board* get_board_ptr(int l, int t, bool c) const {
        return m->get_board_ptr(l,t,c);
    }
    std::vector<std::tuple<int,int,bool,std::string>> get_boards() const;
    generator<vec4> gen_piece_move(vec4 p) const;
    generator<vec4> gen_piece_move(vec4 p, bool c) const;
    std::string to_string() const;
    std::string show_fen() const;
    std::string pretty_l(int l) const;
    std::string pretty_lt(vec4 p0) const;
    
    /*
    parse_move: Given a state `s` and a move in string format `move`, try to parse the move and match it to a unique full_move in the context of state `s`.
    - If successful, return a tuple with first index set to the matched full_move and second index set to the promotion piece if any.
    - If failed, return a tuple with first two indices set to nullopt and the third indices containing all possible matching full_moves. (.size()>1 ~> ambiguous; .size()==0 ~> cannot parse/no match)
    */
    using parse_pgn_res = std::tuple<std::optional<full_move>, std::optional<piece_t>, std::vector<full_move>>;
    parse_pgn_res parse_move(const pgnparser_ast::move &move) const;
    parse_pgn_res parse_move(const std::string &move) const;
};

#endif //STATE_H
