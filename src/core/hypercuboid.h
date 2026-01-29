// created by ftxi on 2025/9/18
// implementing tessaract's hypercuboid algorithm

#ifndef HYPERCUBOID_H
#define HYPERCUBOID_H

#include <map>
#include <set>
#include <vector>
#include <variant>
#include <optional>
#include <string>
#include <memory>
#include <tuple>
#include <functional>
#include "geometry.h"
#include "state.h"
#include "vec4.h"
#include "generator.h"


//generator<moveseq> search_legal_action(state s);

/*
A semimove is one of the following:
- a physical move
- a arriving move
- a departing move; or
- a null move
The axes consist of all possible lines to play a semimove on.
On each axis, there is exactly one semimove being played.
*/

// `b` below is always the new board after move is performed
struct physical_move
{
    full_move m;
    std::shared_ptr<board> b;
};
struct arriving_move
{
    full_move m;
    std::shared_ptr<board> b;
    int idx; // index of corresponding departing move
    // not storing the axis of departing move because it can be found by `line_to_axis[m.from.l()]`
};
struct departing_move
{
    vec4 from;
    std::shared_ptr<board> b;
};
struct null_move
{
    vec4 tl;
};

//AxisLoc
using semimove = std::variant<physical_move, arriving_move, departing_move, null_move>;

[[maybe_unused]]
static std::string show_semimove(semimove loc)
{
    std::ostringstream oss;
    std::visit(overloads {
        [&oss](physical_move ll) {
            oss << "physical_move{m:" << ll.m << ",b:\n";
            oss << ll.b->to_string() << "\n}";
        },
        [&oss](arriving_move ll) {
            oss << "arrriving_move{m:" << ll.m << ",idx=" << ll.idx << ",b:\n";
            oss << ll.b->to_string() << "\n}";
        },
        [&oss](departing_move ll) {
            oss << "departing_move{from:" << ll.from << ",b:\n";
            oss << ll.b->to_string() << "\n}";
        },
        [&oss](null_move ll) {
            oss << "null_move{tl:" << ll.tl << "}";
        }
    }, loc);
    return oss.str();
}

struct HC_info
{
    // local variables
    const state s;
    const std::map<int, int> line_to_axis; // map from timeline index to axis index
    const std::vector<std::vector<semimove>> axis_coords; // axis_coords[i] is the set of all moves on i-th playable board
    const HC universe;
    const int new_axis, dimension; // axes 0, 1, ..., new_axis-1 are playable lines
    // whereas new_axis, new_axis+1, ..., dimension-1 are the possible branching lines
    // identity: dimension = universe.axes.size() = axis_coords.size()
    const std::vector<int> mandatory_lines;
    
private:
    /*
     take_point(): takes a point in hc while making sure arrives matches departures
     if it finds an arrive with its departure no longer in hc, then this arrives get
     deleted immediately (that's why parameter hc is a non-const reference)
     */
    std::optional<point> take_point(HC& hc) const;
    std::optional<slice> find_problem(const point& p, const HC& hc) const;
    std::optional<slice> jump_order_consistent(const point& p, const HC& hc) const;
    std::optional<slice> test_present(const point& p, const HC& hc) const;
    std::optional<slice> find_checks(const point& p, const HC& hc) const;
    moveseq to_action(const point& p) const;
    
    //private aggregate constructor
    HC_info(state s, std::map<int, int> lm, std::vector<std::vector<semimove>> crds, HC uni, int ax, int dim, const std::vector<int> pl)
        : s(std::move(s)), line_to_axis(std::move(lm)), axis_coords(std::move(crds)), universe(std::move(uni)), new_axis(ax), dimension(dim), mandatory_lines(pl) {}

public:
    static std::tuple<HC_info, search_space> build_HC(const state& s);
    generator<moveseq> search(search_space ss) const;
    // /* uncomment when debugging */
    //std::vector<moveseq> search1(search_space ss) const;
};

#endif /* HYPERCUBOID_H */
