#ifndef ACTION_H
#define ACTION_H

#include <variant>
#include <tuple>
#include <set>
#include <ostream>
#include <vector>
#include "vec4.h"
#include "piece.h"

/*
 In this implementation, I use `full_move` instead of `move` to avoid confusion with `std::move`.
 (In contrast, the class `semimove` is defined in hypercuboid.h)
 */
struct full_move
{
    vec4 from, to;
    full_move(vec4 from, vec4 to) : from(from), to(to) {}
    full_move(std::string);
    std::string to_string() const;
    bool operator<(const full_move& other) const;
    bool operator==(const full_move& other) const;
    friend std::ostream& operator<<(std::ostream& os, const full_move& fm);
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
    vec4 get_from() const { return fm.from; }
    vec4 get_to() const { return fm.to; }
    piece_t get_promote() const { return promote_to; }
    std::string to_string() const { return fm.to_string() + static_cast<char>(promote_to);};
    bool operator==(const ext_move&) const = default;
};

/* move sequence (used in hypercuboid.h) */
using moveseq = std::vector<full_move>;

class state; // forward declaration

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
    static int sort(std::vector<ext_move>& mvs, const state &s);
    static action from_vector(const std::vector<ext_move>& mvs, const state &s);
    std::vector<ext_move> get_moves() const { return mvs; }
    int get_length() const { return static_cast<int>(mvs.size()); }
    int get_branching_index() const { return branching_index; }
    bool operator ==(const action &other) const = default;
    friend std::ostream& operator<<(std::ostream &os, const action &act);
};

#endif // ACTION_H
