
#include "hypercuboid.h"
#include "graph.h"


// for debug
#include <cassert>
#include <iostream>
//#define DEBUGMSG
#include "debug.h"

std::shared_ptr<board> extract_board(const semimove& loc)
{
    return std::visit([](const auto& move) -> std::shared_ptr<board> {
        using T = std::decay_t<decltype(move)>;
        
        if constexpr (std::is_same_v<T, physical_move>
                   || std::is_same_v<T, arriving_move>
                   || std::is_same_v<T, departing_move>)
        {
            return move.b;
        }
        else
        {
            assert(false && "shouldn't be a pass here");
            return nullptr;
        }
    }, loc);
}

std::pair<int, int> extract_tl(const semimove& loc)
{
    vec4 p = std::visit(overloads {
        [](physical_move ll) {
            return ll.m.from.tl();
        },
        [](arriving_move ll) {
            return ll.m.to.tl();
        },
        [](departing_move ll) {
            return ll.from.tl();
        },
        [](null_move ll) {
            return ll.tl;
        }
    }, loc);
    return std::make_pair(p.t(), p.l());
}

/*
 gen_move_path: in state s, find the checking path
 return the checking path and sliding_type, where
 - sliding_type = 0 : non-sliding
 - sliding_type = 1 : rook move
 - sliding_type = 2 : bishop move
 - sliding_type = 3 : unicorn move
 - sliding_type = 4 : dragon move
 the endpoints are excluded in checking path
 */
std::tuple<std::vector<vec4>, int> get_move_path(const state &s, full_move fm, int c)
{
    const vec4 p = fm.from, q = fm.to, d = q - p;
    std::shared_ptr<board> b_ptr = s.get_board(p.l(), p.t(), c);
    if(b_ptr->sliding() & pmask(p.xy()))
    {
        // this piece is sliding, makes sense to talk about path
        std::vector<vec4> path;
        vec4 c = vec4(signum(d.x()), signum(d.y()), signum(d.t()), signum(d.l()));
        int sliding_type = c.dot(c);
#ifndef NDEBUG
        {
            assert(p!=q && sliding_type > 0 && sliding_type <= 4);
            std::set tester = {abs(d.l()), abs(d.t()), abs(d.x()), abs(d.y())};
            tester.erase(0);
            assert(tester.size() == 1 && "This doesn't look like a good sliding move");
        }
#endif
        for(vec4 r = p + c; r != q; r = r + c)
        {
            path.push_back(r);
        }
        return std::make_tuple(path, sliding_type);
    }
    else
    {
        return std::make_tuple(std::vector<vec4>(), 0);
    }
}

// test if a royal piece with color c is under attack
bool has_physical_check(const board &b, bool c)
{
    bitboard_t friendly =  c ? b.black() : b.white();
    for(int pos : marked_pos(b.royal() & friendly))
    {
        if([[maybe_unused]] auto x = b.is_under_attack(pos, c))
        {
            dprint("physical check", full_move(vec4(marked_pos(x)[0],vec4(0,0,0,0)),vec4(pos, vec4(0,0,0,0))));
            return true;
        }
    }
    dprint("no check for", c?"black":"white", "in", "\n"+b.to_string());
    return false;
}

std::tuple<HC_info, search_space> HC_info::build_HC(const state& s)
{
    dprint("HC_info::build_HC()");
    std::map<int, int> line_to_axis; // map from timeline index to axis index
    std::vector<std::vector<semimove>> axis_coords; // axis_coords[i] is the set of all moves on i-th playable board
    HC universe;
    int new_axis, dimension;
    std::vector<std::set<int>> nonbranching_axes, branching_axes;
    auto [mandatory_timelines, optional_timelines, unplayable_timelines] = s.get_timeline_status();
    auto playable_timelines = concat_vectors(mandatory_timelines, optional_timelines);
    assert(!s.can_submit());
    auto [present_t, player] = s.get_present();
    
    // generate all moves, then split them into cases
    // for departing moves, we merge the moves that depart from the same coordinate
    std::map<int, std::vector<full_move>> arrives_to, stays_on;
    std::map<int, std::vector<vec4>> departs_from;
    // to track the corresponding departing moves for each arriving move
    std::map<vec4, int> jump_indices;
    
    //TODO: support promotion to other pieces
    static const piece_t promote_to = QUEEN_W;
    const auto &[size_x, size_y] = s.get_board_size();
    
    for(vec4 from : s.gen_movable_pieces())
    {
        bool has_depart = false;
        for(const vec4 &to : s.gen_piece_move(from))
        {
            full_move m(from, to);
            if(from.tl() != to.tl())
            {
                if(!has_depart)
                {
                    departs_from[from.l()].push_back(m.from);
                    has_depart = true;
                }
                arrives_to[to.l()].push_back(m);
            }
            else
            {
                stays_on[from.l()].push_back(m);
            }
        }
        
    }
    
    size_t estimate_size = 1 + arrives_to.size() + departs_from.size();
    
    // build nonbranching axes
    for(int l : playable_timelines)
    {
        std::vector<semimove> locs = {null_move{vec4(0,0,present_t,l)}};
        locs.reserve(estimate_size);
        for(full_move m : stays_on[l])
        {
            vec4 p = m.from, q = m.to;
            vec4 d = q - p;
            const std::shared_ptr<board>& b_ptr = s.get_board(p.l(), p.t(), player);
            std::shared_ptr<board> newboard = nullptr;
            bitboard_t z = pmask(p.xy());
            // en passant
            if((b_ptr->lpawn()&z) && d.x()!=0 && b_ptr->get_piece(q.xy()) == NO_PIECE)
            {
                dprint(" ... en passant");
                newboard = b_ptr->replace_piece(ppos(q.x(),p.y()), NO_PIECE)
                                ->move_piece(p.xy(), q.xy());
            }
            // promotion
            else if((b_ptr->lpawn()&z) && (q.y() == 0 || q.y() == size_y - 1))
            {
                dprint(" ... promotion");
                piece_t promoted = player ? to_black(promote_to) : promote_to;
                newboard = b_ptr->replace_piece(p.xy(), NO_PIECE)
                                ->replace_piece(q.xy(), promoted);
            }
            // castling
            else if((b_ptr->king()&z) && abs(d.x()) > 1)
            {
                dprint(" ... castling");
                int rook_x1 = d.x() < 0 ? 0 : (size_x - 1); //rook's original x coordinate
                int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1); //rook's new x coordinate
                newboard = b_ptr->move_piece(ppos(rook_x1, p.y()), ppos(rook_x2,q.y()))
                                ->move_piece(p.xy(), q.xy());
            }
            // normal move
            else
            {
                dprint(" ... normal move/capture");
                newboard = b_ptr->move_piece(p.xy(), q.xy());
            }
            // filter physical checks in the very begining
            dprint(locs.size(), "physical", m);
            bool flag = has_physical_check(*newboard, player);
            if(!flag)
            {
                locs.push_back(physical_move{m, newboard});
            }
        }
        for(vec4 p : departs_from[l])
        {
            assert(!jump_indices.contains(p));
            // store the departing board after move is made
            std::shared_ptr<board> b_ptr = s.get_board(p.l(), p.t(), player)
                ->replace_piece(p.xy(), NO_PIECE);
            dprint(locs.size(), "depart", p);
            bool flag = has_physical_check(*b_ptr, player);
            if(!flag)
            {
                jump_indices[p] = static_cast<int>(locs.size());
                locs.push_back(departing_move{p, b_ptr});
            }
        }
        for(full_move m : arrives_to[l])
        {
            // only store (possible) non-branching jump arrives
            auto [last_t, last_c] = s.get_timeline_end(m.to.l());
            if(m.to.t() == last_t && player == last_c)
            {
                assert(m.from.tl()!=m.to.tl());
                // store the arriving board after move is made
                vec4 p = m.from, q = m.to;
                piece_t pic = s.get_piece(p, player);
                const std::shared_ptr<board>& c_ptr = s.get_board(q.l(), q.t(), player);
                
                dprint(" ... nonbranching jump");
                std::shared_ptr<board> newboard = c_ptr->replace_piece(q.xy(), pic);
                
                dprint(locs.size(), "arrive", m);
                // use a temporary idx of -1, will be filled later
                bool flag = has_physical_check(*newboard, player);
                if(!flag)
                {
                    locs.push_back(arriving_move{m, newboard, -1});
                }
            }
        }
        // save this axis
        locs.shrink_to_fit();
        line_to_axis[l] = static_cast<int>(axis_coords.size());
        dprint("above in axis", line_to_axis[l]);
        axis_coords.push_back(std::move(locs));
    }
    
    new_axis = static_cast<int>(axis_coords.size());

    // build branching axes
    int max_branch = 0;
    for(const auto& [l, froms] : departs_from)
    {
        // determine the number of branching axes
        if(!froms.empty())
        {
            max_branch++;
        }
    }
    // collect all branching moves
    // the T index for this null_move is needed in find_checks()
    std::vector<semimove> locs = {null_move{vec4(0,0,present_t,s.new_line())}};
    for(const auto &[l, arrives] : arrives_to)
    {
        for(full_move m : arrives)
        {
            vec4 p = m.from, q = m.to;
            piece_t pic = s.get_piece(p, player);
            const std::shared_ptr<board>& c_ptr = s.get_board(q.l(), q.t(), player);
            
            dprint(" ... branching jump");
            std::shared_ptr<board> newboard = c_ptr->replace_piece(q.xy(), pic);
            
            if(jump_indices.contains(m.from))
            {
                /* only add this arriving move when the corresponding departing move
                 is legal (Otherwise, it shouldn't have been registered in jump_map) */
                dprint(locs.size(), "branching:", m);
                bool flag = has_physical_check(*newboard, player);
                if(!flag)
                {
                    locs.push_back(arriving_move{m, newboard, jump_indices[m.from]});
                }
            }
        }
    }
    // replicate this axis max_branch times
    const int new_l = s.new_line();
    const int sign = signum(s.new_line()); // sign for the new lines
    for(int i = 0; i < max_branch; i++)
    {
        assert(!line_to_axis.contains(new_l+sign*i));
        line_to_axis[new_l+sign*i] = new_axis + i;
        axis_coords.push_back(locs);
    }
    dimension = static_cast<int>(axis_coords.size());
    
    // build the whole space
    universe.axes.reserve(dimension);
    for(int n = 0; n < dimension; n++)
    {
        std::set<int> coords;
        // on nth dimension, the hypercube has coordinates 0, 1, ..., m avialible
        // which corresponds to axis_coords[n][0], axis_coords[n][1], ...
        for(int i = 0; i < static_cast<int>(axis_coords[n].size()); i++)
        {
            coords.insert(coords.end(), i);
        }
        universe.axes.push_back(std::move(coords));
    }
    
    // fill the idx of arriving moves
    for(int n = 0; n < static_cast<int>(axis_coords.size()); n++)
    {
        for(int i = 0; i < static_cast<int>(axis_coords[n].size()); i++)
        {
            semimove& loc = axis_coords[n][i];
            if(auto* p = std::get_if<arriving_move>(&loc))
            {
                if(jump_indices.contains(p->m.from))
                {
                    p->idx = jump_indices[p->m.from];
                    assert(line_to_axis.contains(p->m.from.l()));
                    int nfrom = line_to_axis[p->m.from.l()];
                    assert(p->m.from == std::get<departing_move>(axis_coords[nfrom][p->idx]).from);
                }
                else
                {
                    dprint("ghost arrive at axis:", n, ";no:", i,";move pruned:", p->m);
                    auto flag = universe.axes[n].erase(i);
                    assert(flag);
                }
            }
        }
    }
    
#ifdef DEBUGMSG
    for(const auto [k, v]: line_to_axis)
    {
        dprint("line", k, "=>", "axis", v);
    }
#endif
    
    HC_info info(s, line_to_axis, axis_coords, universe, new_axis, dimension, mandatory_timelines);
    
    
    // split the search space by number of branches
    HC hc_n_lines = universe;
    std::set<int> singleton = {0}, non_null;
    if(new_axis < dimension)
    {
        for(int i = 1; i < static_cast<int>(axis_coords[new_axis].size()); i++)
        {
            non_null.insert(non_null.end(), i);
        }
        // non_null = {1,2,...,number of branching moves}
        std::fill(hc_n_lines.axes.begin() + new_axis,
                  hc_n_lines.axes.end(), singleton);
    }
    search_space ss{{hc_n_lines}};
    for(int n = new_axis; n < dimension; n++)
    {
        hc_n_lines.axes[n] = non_null;
        ss.hcs.push_front(hc_n_lines); // prefer lesser branching moves
    }
    return std::make_tuple(info, ss);
}


std::optional<point> HC_info::take_point(HC &hc) const
{
    dprint("take_point()");
    graph g(dimension);
    std::vector<int> must_include;
    // store a pair of departing/arriving move for each edge
    // edge_refs[{p,q}] = the corresponding move on axis p
    std::map<std::pair<int,int>, int> edge_refs;
    point result = std::vector<int>(dimension, -1);
    //build edge_refs and fill default physical moves in result
    for(int n = 0; n < dimension; n++)
    {
        bool has_nonjump = false;
        std::set<int> ghost_arrive_indices;
        for(int i : hc[n])
        {
            const semimove& loc = axis_coords[n][i];
            std::visit(overloads {
                [&](const physical_move&) {
                    if(!has_nonjump)
                    {
                        has_nonjump = true;
                        result[n] = i;
                    }
                },
                [&](const arriving_move& loc) {
                    int from_axis = line_to_axis.at(loc.m.from.l());
                    auto it = hc.axes[from_axis].find(loc.idx);
                    if(it == hc.axes[from_axis].end())
                    {
                        ghost_arrive_indices.insert(i);
                        dprint("ghost arriving move",n,i, "(source", from_axis, loc.idx,")");//,show_semimove(loc));
                        return;
                    }
                    if(!edge_refs.contains(std::make_pair(from_axis, n)))
                    {
                        dprint("new edge", from_axis, n, show_semimove(loc));
                        g.add_edge(from_axis, n);
                        assert(from_axis!=n);
                        edge_refs[std::make_pair(from_axis, n)] = loc.idx;
                        edge_refs[std::make_pair(n, from_axis)] = i;
                        assert(loc.idx != -1);
                    }
                },
                [](const departing_move&) {},
                [&](const null_move&) {
                    if(!has_nonjump)
                    {
                        has_nonjump = true;
                        result[n] = i;
                    }
                },
            }, loc);
        }
        for(int i : ghost_arrive_indices)
        {
            hc.axes[n].erase(i);
        }
        if(hc.axes[n].empty())
        {
            // search space is empty after prune; abort
            return std::nullopt;
        }
        if(!has_nonjump)
        {
            must_include.push_back(n);
        }
    }
    dprint("after prune:", hc.to_string());
    dprint("constructed", g.to_string());
    dprint("must include: ", range_to_string(must_include));
    dprint("intermediate result:", range_to_string(result));
    auto matching = g.find_matching(must_include);
    if(matching)
    {
        dprint("matching", range_to_string(*matching));
        for(const auto& [u,v] : matching.value())
        {
            result[u] = edge_refs[std::make_pair(u,v)];
            result[v] = edge_refs[std::make_pair(v,u)];
        }
#ifndef NDEBUG
        for(int i:result)
        {
            assert(i != -1 && "some axis is still null");
        }
#endif
        dprint("final result:", range_to_string(result));
        assert(hc.contains(result));
#ifdef DEBUGMSG
        for(int n = 0; n < result.size(); n++)
        {
            int i = result[n];
            dprint("n=",n,",i=",i,",loc=",show_semimove(axis_coords[n][i]));
        }
#endif
        return std::optional<point>(result);
    }
    else
    {
        dprint("no match");
        return std::nullopt;
    }
}



std::optional<slice> HC_info::find_problem(const point &p, const HC& hc) const
{
    std::optional<slice> problem = jump_order_consistent(p, hc).or_else([this, &hc, &p]() {
        return test_present(p, hc).or_else([this, &hc, &p]() {
            return find_checks(p, hc);
        });
    });
    return problem;
}

std::optional<slice> HC_info::jump_order_consistent(const point &p, const HC& hc) const
{
    dprint("jump_order_consistent()");
    /* throughout the search, maintain the jump_map of (l, t) => new_l
    so p[new_l] is a arriving move from (l0, t0) that jumps to (l, t) which create a branch
    remember t, l are stored as the higher part of a vec4
     */
    std::map<vec4, int> jump_map;
    auto [t, c] = s.get_present();
    
    for(int n = new_axis; n < dimension; n++)
    {
        // within this loop, n is the axis for new_l
        const int in = p[n];
        const semimove& loc = axis_coords[n][in];
        if(std::holds_alternative<null_move>(loc))
        {
            break;
            // assumed: search space is always separated by number of branches
        }
        assert(std::holds_alternative<arriving_move>(loc));
        const arriving_move arr = std::get<arriving_move>(loc);
        const vec4 from = arr.m.from, to = arr.m.to;
        /* case one: there is a branching move (l0,t0) >> (l',t') ~> new_l, but
         (l',t') is a playable board in which the corresponding played move
         is a pass */
        if(line_to_axis.contains(to.l()))
        {
            const int m = line_to_axis.at(to.l());
            const int im = p[m];
            const semimove& loc2 = axis_coords[m][im];
            if(std::holds_alternative<null_move>(loc2)
               && s.get_timeline_end(to.l()) == std::make_pair(to.t(), c))
            {
                /* ban these combinations:
                 -[m,xm] the null_move on (l', t'); with
                 -[s] any branching move on axis n to (l', t') (which is a null_move)
                 i.e. all moves >> (l',t') then creates branch new_l
                 */
                std::set<int> s;
                for(int i : hc[n])
                {
                    const semimove& loc3 = axis_coords[n][i];
                    if(std::holds_alternative<arriving_move>(loc3))
                    {
                        vec4 to3 = std::get<arriving_move>(loc3).m.to;
                        if(to3.tl() == to.tl())
                        {
                            s.insert(i);
                        }
                    }
                }
                std::map<int, std::set<int>> fixed_axes {{n, s}, {m, {im}}};
                slice problem(fixed_axes);
                dprint("case one; point:", range_to_string(p));
                dprint("problem", problem.to_string());
                assert(problem.contains(p));
                return problem;
            }
        }
        /* case two: there is a branching move (l,t) -> (l',t')
         but the source (l,t) is already registered in jump_map, i.e.
         there was a jump (l0,t0) -> (l,t) ~> (new_l0,t)
         where new_l0 happens latter than new_l
         */
        vec4 critical_tl = from.tl();
        if(jump_map.contains(critical_tl))
        {
            /* ban these combinations:
                -[s1] on axis n, any move starts from (l,t); with
                -[s2] on axis for new_l0, any move goes to (l,t)
            */
            int axis_branch = jump_map[critical_tl];
            std::set<int> s1, s2;
            for(int i : hc.axes[n])
            {
                const semimove& l1 = axis_coords[n][i];
                if(std::holds_alternative<arriving_move>(l1))
                {
                    vec4 from1 = std::get<arriving_move>(l1).m.from;
                    if(from1.tl() == critical_tl)
                    {
                        s1.insert(i);
                    }
                }
            }
            for(int i : hc.axes[axis_branch])
            {
                const semimove& l2 = axis_coords[axis_branch][i];
                if(std::holds_alternative<arriving_move>(l2))
                {
                    vec4 to2 = std::get<arriving_move>(l2).m.to;
                    if(to2.tl() == critical_tl)
                    {
                        s2.insert(i);
                    }
                }
            }
            std::map<int, std::set<int>> fixed_axes {{n, s1}, {axis_branch, s2}};
            slice problem(fixed_axes);
            dprint("case two; point:", range_to_string(p));
            dprint("problem", problem.to_string());
            assert(problem.contains(p));
            return problem;
        }
        // maintain jump_map
        jump_map[to.tl()] = n;
    }
    dprint("no problem");
    return std::nullopt;
}

std::optional<slice> HC_info::test_present(const point &p, const HC& hc) const
{
    dprint("test_present()");
    const auto old_tc = s.get_present();
    const auto [old_present, c] = old_tc;
    // record the lines range and active range of current state
    // these values will get updated as we simulate the process of finding new present
    const auto [l0_min, l0_max] = s.get_initial_lines_range();
    const auto [l_min, l_max] = s.get_lines_range();
    // the following non-const value will be changed and maintained during the loop
    auto [l1_min, l1_max] = s.get_lines_range();
    auto [active_min, active_max] = s.get_active_range();
    // step one: find the new present
    int mint = old_present; // mint is the new present
    std::optional<std::pair<int,int>> pass_coord = std::nullopt; // record the axis of the problematic pass
    std::optional<int> reactivate_move_axis = std::nullopt;
    for(int l : mandatory_lines)
    {
        assert(line_to_axis.contains(l));
        int n = line_to_axis.at(l);
        if(std::holds_alternative<null_move>(axis_coords[n][p[n]]))
        {
            // if there is a pass in playable line, then it could be problematic
            pass_coord = {n, p[n]};
        }
    }
    for(int n = new_axis; n < dimension; n++)
    {
        // for all branching moves
        int i = p[n];
        // present may need to move to the time of this arrive
        semimove loc = axis_coords[n][i];
        if(std::holds_alternative<null_move>(loc))
        {
            break;
        }
        // update lines range and active range
        std::optional<int> reactivated = std::nullopt;
        int l_new;
        if(c == false)
        {
            l1_max++;
            l_new = l1_max;
        }
        else
        {
            l1_min--;
            l_new = l1_min;
        }
        int whites_lines = l1_max - l0_max;
        int blacks_lines = l0_min - l1_min;
        if(l_new > l0_max && whites_lines <= blacks_lines + 1 && l_new > active_max)
        {
            dprint("+");
            active_max++;
            if(l1_min < active_min) // check reactivate
            {
                active_min--;
                reactivated = active_min;
            }
        }
        else if(l_new < l0_min && blacks_lines <= whites_lines + 1 && l_new < active_min)
        {
            dprint("-");
            active_min--;
            if(l1_max > active_max)
            {
                active_max++;
                reactivated = active_max;
            }
        }
        
        auto [t, l] = extract_tl(loc);
        dprint(t, mint, active_min, l_new, active_max);
        // only care when new line is active
        if(t < mint && active_min <= l_new && l_new <= active_max)
        {
            mint = t;
            // if the move jumps backward, the previous threat is eliminated
            pass_coord = std::nullopt;
            reactivate_move_axis = std::nullopt;
        }
        // there is probably a newly activated line after this branching move
        // which moves the present
        if(reactivated)
        {
            const auto [newline_t, newline_c] = s.get_timeline_end(*reactivated);
            if(newline_t < mint && newline_c == c)
            {
                mint = newline_t;
                // could be a problem if played a pass on reactivated line
                assert(line_to_axis.contains(*reactivated));
                int n1 = line_to_axis.at(*reactivated);
                if(std::holds_alternative<null_move>(axis_coords[n1][p[n1]]))
                {
                    pass_coord = {n1, p[n1]};
                    reactivate_move_axis = n;
                }
            }
        }
    }
    if(pass_coord)
    {
        slice problem;
        /* on the axis for pass, ban this pass */
        const auto [pass_n, pass_i] = *pass_coord;
        problem.fixed_axes[pass_n] = {pass_i};
        //if(pass_n >= new_axis)
        
        /*
         on new axes (except for the reactivate_move_axis in cases where pass belongs to a reactivated line):
         ban all moves that doesn't create an *active* branch before the pass's time
         i.e. if this_axis - new_axis + 1 <= opponents_timeline - players_timeline + 1, ban every pass/arrives later than this line's time
         otherwise, ban everything
         
         (this_axis - new_axis + 1 <=  opponents_timeline - players_timeline + 1) reduces to
         this_axis <= opponents_timeline - players_timeline + new_axis
         */
        int whites_lines = l_max - l0_max;
        int blacks_lines = l0_min - l_min;
        int timeline_advantage = c ? (whites_lines - blacks_lines) : (blacks_lines - whites_lines);
        
        for(int n = new_axis; n <= std::min(timeline_advantage+new_axis, dimension-1); n++)
        {
            if(reactivate_move_axis == std::make_optional<int>(n))
            {
                continue;
            }
            std::set<int> s;
            for(int i : hc[n])
            {
                semimove loc = axis_coords[n][i];
                if(std::holds_alternative<null_move>(loc))
                {
                    s.insert(i);
                    // actually I can just break two loops ...
                }
                else
                {
                    auto am = std::get<arriving_move>(loc);
                    if(am.m.to.t() >= mint)
                    {
                        s.insert(i);
                    }
                }
            }
            problem.fixed_axes[n] = s;
        
        }
        dprint("point:", range_to_string(p));
        dprint("problem", problem.to_string());
        assert(problem.contains(p));
        return problem;
    }
    else
    {
        return std::nullopt;
    }
}

std::optional<slice> HC_info::find_checks(const point &p, const HC& hc) const
{
    dprint("HC_info::find_checks()");
    auto [t, c] = s.get_present();
    moveseq mvs = to_action(p);
    // apply moves in-place on internal state and record logs for undo
    std::vector<state::apply_log> logs;
    #ifdef DEBUGMSG
    std::string mvsstr;
    #endif
    for(full_move mv : mvs)
    {
        #ifdef DEBUGMSG
            mvsstr += s.pretty_move<state::SHOW_NOTHING>(mv) + " ";
        #endif
        logs.push_back(s.apply_move_inplace(mv));
    }
    auto submit_rec = s.submit_with_record();
    dprint("applied moves:", mvsstr);
    dprint("c=", c);
    auto gen = s.find_checks(!c);
    if(auto maybe_check = gen.first())
    {
        // there is a check
        // the slice to remove is a product of coordinates on certain axes
        full_move check = maybe_check.value();
        dprint("found check: ", check);
        assert(check.from.tl() != check.to.tl() && "physical checks should have already removed");
        auto [path, sliding_type] = get_move_path(s, check, !c);
        auto is_next = c ? [](int t1, int t2){
            return t1 + 1 == t2;
        }:[](int t1, int t2){
            return t1 == t2;
        };
        /* on axis for check.from.l():
         ban all moves that allows the same piece hostile piece remain in that position
         */
        slice problem;
        if(line_to_axis.contains(check.from.l()))
        {
            int n1 = line_to_axis.at(check.from.l());
            std::set<int> not_taking;
            for(int i : hc.axes[n1])
            {
                semimove loc = axis_coords[n1][i];
                /* if there isn't a new board on the same place, it won't create the same check*/
                if(std::holds_alternative<null_move>(loc) || !is_next(extract_tl(loc).first,check.from.t()))
                {
                    continue;
                }
                std::shared_ptr<board> newboard = extract_board(loc);
                if(sliding_type)
                {
                    bitboard_t bb = c ? newboard->white() : newboard->black();
                    switch(sliding_type)
                    {
                        case 1:
                            bb &= newboard->lrook();
                            break;
                        case 2:
                            bb &= newboard->lbishop();
                            break;
                        case 3:
                            bb &= newboard->lunicorn();
                            break;
                        case 4:
                            bb &= newboard->ldragon();
                            break;
                        default:
                            assert(false && "wrong sliding type infered");
                    }
                    if(pmask(check.from.xy()) & bb)
                    {
                        dprint(n1, i, sliding_type, show_semimove(loc));
                        dprint("axis", n1, "not taking (sliding)", i);
                        not_taking.insert(i);
                    }
                }
                else if(newboard->get_piece(check.from.xy()) == s.get_piece(check.from, !c))
                {
                    // non sliding pieces remains in same position
                    dprint(n1, i, sliding_type, show_semimove(loc));
                    dprint("axis", n1, "not taking (untouched)", i);
                    not_taking.insert(i);
                }
            }
            problem.fixed_axes.insert({n1, not_taking});
        }
        /* on axis for check.to.l():
         if the board of checks.to is what just played, ban all moves that leave
         a royal piece on check.to.
         Otherwise, don't alter that axis futher.
         */
        int l2 = check.to.l();
//        if(p == std::vector<int>{2, 12, 8, 9, 0, 22, 0, 0, 0, 0})
//        {
//            std::cout << "ach!\n";
//        }
        if(line_to_axis.contains(l2))
        {
            int n2 = line_to_axis.at(l2);
            semimove loc0 = axis_coords[n2][p[n2]];
            if(std::holds_alternative<null_move>(loc0) || !is_next(extract_tl(loc0).first, check.to.t()))
            {
                // pass to ban everything
            }
            else
            {
                std::set<int> expose_royal;
                for(int i : hc.axes[n2])
                {
                    semimove loc = axis_coords[n2][i];
                    std::shared_ptr<board> newboard;
                    /* if there isn't a new board on the same place, do nothing*/
                    if(std::holds_alternative<null_move>(loc) || !is_next(extract_tl(loc).first, check.to.t()))
                    {
                        continue;
                    }
                    else
                    {
                        newboard = extract_board(loc);
                    }
                    bitboard_t friendly = c ? newboard->black() : newboard->white();
                    bool is_royal = pmask(check.to.xy()) & newboard->royal() & friendly;
                    if(is_royal)
                    {
                        dprint(show_semimove(loc));
                        dprint("axis", n2, "expose royal", i);
                        expose_royal.insert(i);
                    }
                }
                problem.fixed_axes.insert({n2, expose_royal});
            }
        }
        /* on axes for checking path crossings:
         if passing board is prone to change, then ban all moves, except for
         those (physical or arriving) brings a non-royal piece blocking the
         crossed square. Otherwise, do nothing as before
         */
        for(vec4 crossed : path)
        {
//            dprint("should cross", crossed.l());
            if(line_to_axis.contains(crossed.l()))
            {
                int n = line_to_axis.at(crossed.l());
                semimove loc0 = axis_coords[n][p[n]];
                if(std::holds_alternative<null_move>(loc0) || !is_next(extract_tl(loc0).first, crossed.t()))
                {
                    // pass to ban everything
                }
                else
                {
                    bitboard_t z = pmask(crossed.xy());
                    std::set<int> not_blocking;
                    for(int i : hc.axes[n])
                    {
                        semimove loc = axis_coords[n][i];
                        /* if there isn't a board, then nothing pass through it*/
                        if(std::holds_alternative<null_move>(loc) || !is_next(extract_tl(loc).first, crossed.t()))
                        {
                            continue;
                        }
                        std::shared_ptr<board> newboard = extract_board(loc);
                        /* if the very place is empty, then it is clearly not blocking*/
                        if(!(z & newboard->occupied()))
                        {
                            dprint(n, i, sliding_type, show_semimove(loc));
                            dprint("axis", n, "not blocking (empty)", i);
                            not_blocking.insert(i);
                            continue;
                        }
                        /* on the crossed point, if a hostile piece with the same sliding type
                         of the attacking piece is placed here, it doesn't completely resolve the check
                         */
                        if(sliding_type)
                        {
                            bitboard_t bb = c ? newboard->white() : newboard->black();
                            switch(sliding_type)
                            {
                                case 1:
                                    bb &= newboard->lrook();
                                    break;
                                case 2:
                                    bb &= newboard->lbishop();
                                    break;
                                case 3:
                                    bb &= newboard->lunicorn();
                                    break;
                                case 4:
                                    bb &= newboard->ldragon();
                                    break;
                                default:
                                    assert(false && "wrong sliding type infered");
                            }
                            if(z & bb)
                            {
                                dprint(n, i, sliding_type, show_semimove(loc));
                                dprint("axis", n, "not blocking (sliding)", i);
                                not_blocking.insert(i);
                                continue;
                            }
                        }
                        /* on the crossed point, if a friendly royal piece is there
                         it is still checking despite the path is technically blocked
                         */
                        bitboard_t friendly = c ? newboard->black() : newboard->white();
                        bool is_royal = pmask(check.to.xy()) & newboard->royal() & friendly;
                        if(z & is_royal)
                        {
                            dprint(n, i, sliding_type, show_semimove(loc));
                            dprint("axis", n, "not blocking (expose royal)", i);
                            not_blocking.insert(i);
                            continue;
                        }
                    }
                    problem.fixed_axes[n] = not_blocking;
                }
            }
        }
        dprint("point:", range_to_string(p));
        dprint("problem", problem.to_string());
        assert(problem.contains(p));
        // restore state before returning
        s.undo_submit(submit_rec);
        s.undo_moves_inplace(logs);
        return problem;
    }
    dprint("no checks found");
    // restore state
    s.undo_submit(submit_rec);
    s.undo_moves_inplace(logs);
    return std::nullopt;
}

moveseq HC_info::to_action(const point &p) const
{
    std::vector<full_move> mvs;
    for(const auto &[l,i] : line_to_axis)
    {
        semimove loc = axis_coords[i][p[i]];
        if(std::holds_alternative<physical_move>(loc))
        {
            mvs.push_back(std::get<physical_move>(loc).m);
        }
        else if(std::holds_alternative<arriving_move>(loc))
        {
            mvs.push_back(std::get<arriving_move>(loc).m);
        }
    }
    auto [t,c] = s.get_present();
    if(c)
    {
        std::reverse(mvs.begin(), mvs.end());
    }
    return mvs;
}



// ------------------------------------------------------------


generator<moveseq> HC_info::search(search_space ss) const
{
    dprint("begining search: ", ss.to_string());
    while(!ss.hcs.empty())
    {
        HC hc = ss.hcs.back();
        dprint("searching ", hc.to_string());
        ss.hcs.pop_back();
        auto pt_opt = take_point(hc);
        if(pt_opt)
        {
            point pt = pt_opt.value();
            dprint("got point: ", range_to_string(pt));
            auto problem = find_problem(pt, hc);
            if(problem)
            {
                dprint("found problem:", problem.value().to_string());
                // remove the problematic slice from hc, and add the remaining to ss
                search_space new_ss = hc.remove_slice(problem.value());
                // make sure when a leave is removed, so is the corresponding arrive
                dprint("removed problem, continue search:", new_ss.to_string());
                ss.concat(std::move(new_ss));
            }
            else
            {
                dprint("point is okay, removing it from this hc");
                co_yield to_action(pt);
                search_space new_ss = hc.remove_point(pt);
                dprint("removed point, continue search:", new_ss.to_string());
                ss.concat(std::move(new_ss));
            }
        }
        else
        {
            dprint("didn't secure any point in the first hypercuboid;");
            dprint("continue searching the remaining part");
        }
    }
    dprint("search space is empty; finish.");
    co_return;
}

// /* for debugging only (this version is more friendly with stacktracing) */
//std::vector<moveseq> HC_info::search1(search_space ss) const
//{
//    std::vector<moveseq> result;
//    adprint("begining search: ", ss.to_string());
//    while(!ss.hcs.empty())
//    {
//        HC hc = ss.hcs.back();
//        adprint("searching ", hc.to_string());
//        ss.hcs.pop_back();
//        auto pt_opt = take_point(hc);
//        if(pt_opt)
//        {
//            point pt = pt_opt.value();
//            print_range("got point: ", pt);
//            auto problem = find_problem(pt, hc);
//            if(problem)
//            {
//                adprint("found problem:", problem.value().to_string());
//                // remove the problematic slice from hc, and add the remaining to ss
//                search_space new_ss = hc.remove_slice(problem.value());
//                // make sure when a leave is removed, so is the corresponding arrive
//                adprint("removed problem, continue search:", new_ss.to_string());
//                ss.concat(std::move(new_ss));
//            }
//            else
//            {
//                adprint("point is okay, removing it from this hc");
//                result.push_back(to_action(pt));
//                search_space new_ss = hc.remove_point(pt);
//                adprint("removed point, continue search:", new_ss.to_string());
//                ss.concat(std::move(new_ss));
//            }
//        }
//        else
//        {
//            dprint("didn't secure any point in the first hypercuboid;");
//            dprint("continue searching the remaining part");
//        }
//    }
//    dprint("search space is empty; finish.");
//    return result;
//}

