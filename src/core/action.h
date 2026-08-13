#ifndef ACTION_H
#define ACTION_H

#include <variant>
#include <tuple>
#include <set>
#include <ostream>
#include <vector>
#include "vec4.h"
#include "piece.h"

class state;
class action;

namespace pgn_options {
constexpr uint16_t SHOW_NOTHING   = 0;
constexpr uint16_t SHOW_RELATIVE  = 1 << 0;
constexpr uint16_t SHOW_PAWN      = 1 << 1;
constexpr uint16_t SHOW_CAPTURE   = 1 << 2;
constexpr uint16_t SHOW_PROMOTION = 1 << 3;
constexpr uint16_t SHOW_MATE      = 1 << 4;
constexpr uint16_t SHOW_LCOMMENT  = 1 << 5;
constexpr uint16_t SHOW_SHORT     = 1 << 6;
constexpr uint16_t SHOW_ALL = SHOW_RELATIVE | SHOW_PAWN | SHOW_CAPTURE | SHOW_PROMOTION |
                              SHOW_MATE | SHOW_LCOMMENT | SHOW_SHORT;
}

/*
 In this implementation, I use `full_move` instead of `move` to avoid confusion with `std::move`.
 (In contrast, the class `semimove` is defined in semimove.h)
 */
struct full_move
{
    vec4 from, to;
    full_move(vec4 from, vec4 to) : from(from), to(to) {}
    full_move(std::string);
    std::string to_string() const;
    std::string lan(const state &, piece_t promote_to=QUEEN_W) const;
    template<uint16_t OPTIONS>
    std::string pgn(const state &, piece_t promote_to=QUEEN_W) const;
    std::string pgn(const state &, piece_t promote_to=QUEEN_W,
                    uint16_t options=pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION) const;
private:
    template<uint16_t OPTIONS>
    std::string pgn_impl(const state &, piece_t promote_to, char check_symbol, bool multimove) const;
    friend class action;
public:
    bool operator<(const full_move &other) const;
    bool operator==(const full_move &other) const;
    friend std::ostream &operator<<(std::ostream &os, const full_move &fm);
};

/*
 An extended move is a move with additional promotion information
 */
struct ext_move
{
    full_move fm;
    piece_t promote_to;
    ext_move(full_move fm, piece_t promote_to=QUEEN_W) : fm(fm), promote_to(promote_to) {}
    ext_move(vec4 from, vec4 to, piece_t promote_to=QUEEN_W) : fm{from, to}, promote_to(promote_to) {}
    ext_move(std::string);
    vec4 get_from() const { return fm.from; }
    vec4 get_to() const { return fm.to; }
    piece_t get_promote() const { return promote_to; }
    std::string to_string() const;
    std::string lan(const state &) const;
    template<uint16_t OPTIONS>
    std::string pgn(const state &) const;
    std::string pgn(const state &, uint16_t options=pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION) const;
    bool operator==(const ext_move&) const = default;
};

/* move sequence (used in hypercuboid.h) */
using moveseq = std::vector<full_move>;

/*
 An action is a sequence of extended moves sorted in standard order
 `branching_index` is the index of index branching move
 (no branching move => branching_index = mvs.size())
 i.e. mvs[0], ..., mvs[branching_index-1] ~> non-branching
      mvs[branching_index], ..., mvs[mvs.size()-1] ~> branching
 
 To construct an instance, use fatory `from_vector`.
 */
class action
{
    std::vector<ext_move> mvs;
    int branching_index;
    action(std::vector<ext_move> mvs) : mvs(mvs) {}
public:
    action() : mvs{}, branching_index{0} {}
    /* Sort a vector of extended moves according to the standard order
    as a side effact and return the branching index */
    static int sort(std::vector<ext_move> &mvs, const state &s);
    static action from_vector(const std::vector<ext_move> &mvs, const state &s);
    std::vector<ext_move> get_moves() const { return mvs; }
    int get_length() const { return static_cast<int>(mvs.size()); }
    int get_branching_index() const { return branching_index; }
    std::string to_string() const;
    std::string lan(const state &) const;
    template<uint16_t OPTIONS>
    std::string pgn(const state &) const;
    std::string pgn(const state &, uint16_t options=pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION) const;
    bool operator ==(const action &other) const = default;
    friend std::ostream &operator<<(std::ostream &os, const action &act);
};

#endif // ACTION_H
