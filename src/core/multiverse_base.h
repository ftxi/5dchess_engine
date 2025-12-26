//
//  board_2d.h
//  5dchess_engine
//
//  Created by ftxi on 2024/12/5.
//

#ifndef MULTIVERSE_BASE_H
#define MULTIVERSE_BASE_H

#include <string>
#include <vector>
#include <tuple>
#include <utility>
#include <map>
#include <memory>
#include "turn.h"
#include "board.h"
#include "vec4.h"
#include "generator.h"

using movegen_t = generator<std::pair<vec4, bitboard_t>>;
using boards_info_t = std::tuple<int,int,bool,std::string>; // l, t, color, fen

/*
 The multiverse class.

 Behavior of copying a multiverse object is just copy the vector of vectors of pointers to the boards. It does not perform deep-copy of a board object. (Which is expected.)

 This is an abstract base class. Two child classes are defined for odd and even timelines respectively.
 */
class multiverse
{
private:
    const int size_x, size_y; // board size
    //const int l0_min, l0_max; // initial timeline range
    std::vector<std::vector<std::shared_ptr<board>>> boards;
    // the following data are derivated from boards:
    int l_min, l_max, active_min, active_max;
    std::vector<int> timeline_start, timeline_end;
    
    // private methods for move generation
    template<piece_t P, bool C>
    bitboard_t gen_physical_moves_impl(vec4 p) const;

    template<piece_t P, bool C, bool ONLY_SP>
    movegen_t gen_moves_impl(vec4 p) const;

    template<bool C>
    generator<vec4> gen_board_move_impl(vec4 p0) const;

    /*
     generate sliding moves in directions that are:
      + starting from `p`
      + in superphysical directions, moves as axesmode `TL`
        - when `TL` is `ORTHOGONAL`, moves to p+(*,*,1,0), p+(*,*,1,0), etc.
        - when `TL` is `DIAGONAL`, moves to p+(*,*,1,1), p+(*,*,1,-1), etc.
        - `BOTH` means both of the above
      + in physical directions, moves as axesmode `XY`
        - when `XY` is `ORTHOGONAL`, moves to p+(1,0,*,*), p+(0,1,*,*), etc.
        - when `XY` is `DIAGONAL`, moves to p+(1,1,*,*), p+(1,-1,*,*), etc.
        - `BOTH` means both of the above
     */
    enum class axesmode {ORTHOGONAL, DIAGONAL, BOTH};
    template<bool C, axesmode TL, axesmode XY>
    void gen_compound_moves(vec4 p, std::map<vec4, bitboard_t>& result) const;

    template<bool C>
    std::vector<std::pair<vec4, bitboard_t>> gen_purely_sp_rook_moves(vec4 p0) const;
    
    template<bool C>
    std::vector<std::pair<vec4, bitboard_t>> gen_purely_sp_bishop_moves(vec4 p0) const;
    
    template<bool C>
    std::vector<std::pair<vec4, bitboard_t>> gen_purely_sp_knight_moves(vec4 p0) const;

    void insert_board_impl(int l, int t, bool c, const std::shared_ptr<board>& b_ptr);
protected:
    virtual std::pair<int,int> calculate_active_range() const = 0;
    void update_active_range(); // for initialization of derived classes only
public:
    // constructor
    multiverse(std::vector<boards_info_t> boards, int size_x, int size_y);
    
    // modifiers
    void insert_board(int l, int t, bool c, const std::shared_ptr<board>& b_ptr);
    void append_board(int l, const std::shared_ptr<board>& b_ptr);
    // undo helpers
    void pop_board(int l);
    void remove_line(int l);

    // getters
    std::pair<int, int> get_board_size() const;
    virtual std::pair<int, int> get_initial_lines_range() const = 0;
    std::pair<int, int> get_lines_range() const;
    std::pair<int, int> get_active_range() const;
    turn_t get_timeline_start(int l) const;
    turn_t get_timeline_end(int l) const;
    
    std::shared_ptr<board> get_board(int l, int t, bool c) const;
    
    template<bool SHOW_UMOVE=false>
    std::vector<boards_info_t> get_boards() const;
    
    std::string to_string() const;
    piece_t get_piece(vec4 a, bool color) const;
    bool get_umove_flag(vec4 a, bool color) const;
    
    /*
     This helper function returns (present_t, present_c)
     where: present_t is the time of present in L,T coordinate
            present_c is either false (for white) or true (for black)
     */
    turn_t get_present() const;
    
    // move generation
    template<bool C> bitboard_t gen_physical_moves(vec4 p) const;
    template<bool C> movegen_t gen_superphysical_moves(vec4 p) const;
    template<bool C> movegen_t gen_moves(vec4 p) const;
    generator<vec4> gen_piece_move(vec4 p, bool board_color) const;
    
    // help functions
    bool inbound(vec4 a, bool color) const;
    virtual std::unique_ptr<multiverse> clone() const = 0;
    virtual std::string pretty_l(int l) const = 0;
    virtual std::string pretty_lt(vec4 p0) const = 0;
    virtual ~multiverse() = default;
};

#endif /* MULTIVERSE_BASE_H */
