
#include "hypercuboid.h"
#include "check_position.h"
#include "move_feature.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <memory_resource>
#include <random>
#include <utility>

//#define DEBUGMSG
#include "debug.h"

semimove HC_info::to_semimove(const entry &loc)
{
    return std::visit([](const auto &move) -> semimove {
        using T = std::decay_t<decltype(move)>;
        if constexpr (std::is_same_v<T, physical_entry>) return physical_move{move.m};
        else if constexpr (std::is_same_v<T, arriving_entry>) return arriving_move{move.m};
        else if constexpr (std::is_same_v<T, departing_entry>) return departing_move{move.from};
        else return null_move{};
    }, loc);
}

const board &HC_info::extract_board(const entry &loc)
{
    return std::visit([](const auto &move) -> const board & {
        using T = std::decay_t<decltype(move)>;
        if constexpr (std::is_same_v<T, physical_entry>
                   || std::is_same_v<T, arriving_entry>
                   || std::is_same_v<T, departing_entry>) return move.b;
        else {
            assert(false && "shouldn't extract a board from a null entry");
            throw std::logic_error("cannot extract a board from a null HC entry");
        }
    }, loc);
}

std::pair<int, int> HC_info::extract_tl(const entry &loc)
{
    const vec4 p = to_semimove(loc).hotspot();
    return {p.t(), p.l()};
}

semimove HC_info::get_semimove(index_t n, index_t i) const
{
    return to_semimove(axis_coords.at(n).at(i));
}

std::optional<HC_info::move_boards> HC_info::get_move_boards(index_t axis, index_t coordinate) const
{
    const auto &loc = axis_coords.at(axis).at(coordinate);
    if (const auto *physical = std::get_if<physical_entry>(&loc))
        return move_boards{physical->m, &physical->b, nullptr};
    if (const auto *arrival = std::get_if<arriving_entry>(&loc)) {
        const auto source_axis = line_to_axis_unsafe(arrival->m.from.l());
        assert(source_axis != no_axis);
        const auto &departure = std::get<departing_entry>(axis_coords[source_axis].at(arrival->idx));
        return move_boards{arrival->m, &arrival->b, &departure.b};
    }
    return std::nullopt;
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
std::tuple<std::vector<vec4>, int> get_move_path(const check_position &s, full_move fm, int c)
{
    const vec4 p = fm.from, q = fm.to, d = q - p;
    const board *b_ptr = s.get_board_ptr(p.l(), p.t(), c);
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

HC_info HC_info::build_info(const state &s, HC_build_options options)
{
    dprint("HC_info::build_HC()");
    HC_info info(s);
    std::vector<int> optional_timelines, unplayable_timelines;
    std::tie(info.mandatory_lines, optional_timelines, unplayable_timelines)
        = s.get_timeline_status();
    auto playable_timelines = concat_vectors(info.mandatory_lines, optional_timelines);
    assert(!s.test_present());
    const bool player = s.get_present().c();

    const auto [l_min, l_max] = s.get_lines_range();
    // At most one branch axis is needed per existing playable timeline.
    info.line_axes.reserve(static_cast<size_t>(l_max - l_min + 1) + playable_timelines.size());
    info.line_axes.resize(l_max - l_min + 1, no_axis);
    for(index_t axis = 0; axis < static_cast<index_t>(playable_timelines.size()); axis++)
    {
        info.line_axes[playable_timelines[axis] - info.l0_offset] = axis;
    }

    std::array<std::byte, 32 * 1024> temporary_storage;
    std::pmr::monotonic_buffer_resource temporary_resource(
        temporary_storage.data(), temporary_storage.size());

    // generate all moves, then split them into cases
    // for departing moves, we merge the moves that depart from the same coordinate
    struct arrival_bucket
    {
        int line;
        std::pmr::vector<full_move> moves;

        arrival_bucket(int line, std::pmr::memory_resource *resource)
            : line(line), moves(resource) {}
    };
    struct axis_moves
    {
        std::pmr::vector<full_move> stays;
        std::pmr::vector<vec4> departures;

        explicit axis_moves(std::pmr::memory_resource *resource)
            : stays(resource), departures(resource) {}
    };

    std::pmr::vector<arrival_bucket> arrivals(&temporary_resource);
    arrivals.reserve(
        info.mandatory_lines.size()
        + optional_timelines.size()
        + unplayable_timelines.size());
    std::pmr::vector<axis_moves> moves_by_axis(&temporary_resource);
    moves_by_axis.reserve(playable_timelines.size());
    for(size_t i = 0; i < playable_timelines.size(); i++)
    {
        moves_by_axis.emplace_back(&temporary_resource);
    }

    auto add_arrival = [&arrivals, &temporary_resource](full_move move)
    {
        const int line = move.to.l();
        auto bucket = std::find_if(
            arrivals.begin(), arrivals.end(),
            [line](const arrival_bucket &candidate) { return candidate.line == line; });
        if(bucket == arrivals.end())
        {
            arrivals.emplace_back(line, &temporary_resource);
            bucket = std::prev(arrivals.end());
        }
        bucket->moves.push_back(move);
    };

    // to track the corresponding departing moves for each arriving move
    struct jump_index
    {
        vec4 from;
        index_t entry;
    };
    std::pmr::vector<jump_index> jump_indices(&temporary_resource);

    auto find_jump_index = [&jump_indices](vec4 from) -> const index_t *
    {
        auto result = std::find_if(
            jump_indices.begin(), jump_indices.end(),
            [from](const jump_index &candidate) { return candidate.from == from; });
        return result == jump_indices.end() ? nullptr : &result->entry;
    };
    
    const auto [size_x, size_y] = s.get_board_size();
    
    for(vec4 from : s.get_all_pieces(playable_timelines))
    {
        const index_t source_axis = info.line_to_axis_unsafe(from.l());
        bool has_depart = false;
        for(const vec4 &to : s.gen_piece_move(from))
        {
            full_move m(from, to);
            if(from.tl() != to.tl())
            {
                if(!has_depart)
                {
                    moves_by_axis[source_axis].departures.push_back(m.from);
                    has_depart = true;
                }
                add_arrival(m);
            }
            else if(!options.prune_optional_moves || source_axis < info.mandatory_lines.size())
            {
                moves_by_axis[source_axis].stays.push_back(m);
            }
        }
    }

    std::sort(
        arrivals.begin(),
        arrivals.end(),
        [](const arrival_bucket &lhs, const arrival_bucket &rhs)
        {
            return lhs.line < rhs.line;
        }
    );
    auto find_arrivals = [&arrivals](int line) -> const std::pmr::vector<full_move> *
    {
        auto bucket = std::lower_bound(
            arrivals.begin(), arrivals.end(), line,
            [](const arrival_bucket &candidate, int value)
            {
                return candidate.line < value;
            });
        return bucket == arrivals.end() || bucket->line != line
            ? nullptr : &bucket->moves;
    };

    index_t max_branch = 0;
    size_t departure_count = 0;
    for(const auto &moves : moves_by_axis)
    {
        departure_count += moves.departures.size();
        if(!moves.departures.empty()) max_branch++;
    }
    assert(playable_timelines.size() + max_branch <= no_axis);
    if(player)
    {
        info.line_axes.insert(info.line_axes.begin(), max_branch, no_axis);
        info.l0_offset -= static_cast<int>(max_branch);
    }
    else
    {
        info.line_axes.resize(info.line_axes.size() + max_branch, no_axis);
    }
    jump_indices.reserve(departure_count);
    info.axis_coords.reserve(playable_timelines.size() + max_branch);

    // build nonbranching axes
    for(index_t axis = 0; axis < static_cast<index_t>(playable_timelines.size()); axis++)
    {
        const int l = playable_timelines[axis];
        const auto *arriving_moves = find_arrivals(l);
        assert(info.line_to_axis_unsafe(l) == info.axis_coords.size());
        auto &locs = info.axis_coords.emplace_back();
        locs.reserve(
            1 + moves_by_axis[axis].stays.size()
            + moves_by_axis[axis].departures.size()
            + (arriving_moves ? arriving_moves->size() : 0));
        locs.push_back(null_entry{});
        for(full_move m : moves_by_axis[axis].stays)
        {
            vec4 p = m.from, q = m.to;
            vec4 d = q - p;
            const board &source_board = *s.get_board_ptr(p.l(), p.t(), player);
            dprint(locs.size(), "physical", m);
            locs.emplace_back(std::in_place_type<physical_entry>, m, source_board);
            board &newboard = std::get<physical_entry>(locs.back()).b;
            bitboard_t z = pmask(p.xy());
            // en passant
            if((source_board.lpawn()&z) && d.x()!=0
               && source_board.get_piece(q.xy()) == NO_PIECE)
            {
                dprint(" ... en passant");
                newboard.set_piece(ppos(q.x(),p.y()), NO_PIECE);
                piece_t piece = newboard.get_piece(p.xy());
                newboard.set_piece(q.xy(), piece);
                newboard.set_piece(p.xy(), NO_PIECE);
            }
            // castling
            else if((source_board.king()&z) && abs(d.x()) > 1)
            {
                dprint(" ... castling");
                const bitboard_t side = d.x() < 0 ? z - 1 : ~(z - 1);
                int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1); //rook's new x coordinate
                const int rook_from = bb_get_pos(source_board.castling_rooks(p.xy(), player) & side);
                const int rook_to = ppos(rook_x2, q.y());
                piece_t rook = newboard.get_piece(rook_from);
                newboard.set_piece(rook_to, rook);
                newboard.set_piece(rook_from, NO_PIECE);
                piece_t king = newboard.get_piece(p.xy());
                newboard.set_piece(q.xy(), king);
                newboard.set_piece(p.xy(), NO_PIECE);
            }
            // normal move
            else
            {
                dprint(" ... normal move/capture");
                piece_t piece = newboard.get_piece(p.xy());
                newboard.set_piece(q.xy(), piece);
                newboard.set_piece(p.xy(), NO_PIECE);
            }
            // Legality checking only needed occupancy here. Outgoing-check scoring
            // also needs the promoted piece's attacks, so cache the default promotion.
            if ((source_board.lpawn() & z) && (q.y() == (player ? 0 : size_y - 1))) {
                const piece_t promotion = ext_move(m,s).promote_to;
                if (promotion != NO_PIECE)
                    newboard.set_piece(q.xy(), player ? to_black(promotion) : promotion);
            }
            // filter physical checks in the very begining
            if(has_physical_check(newboard, player))
            {
                locs.pop_back();
            }
        }
        for(vec4 p : moves_by_axis[axis].departures)
        {
            assert(find_jump_index(p) == nullptr);
            // store the departing board after move is made
            const index_t entry_index = static_cast<index_t>(locs.size());
            locs.emplace_back(
                std::in_place_type<departing_entry>, p,
                *s.get_board_ptr(p.l(), p.t(), player));
            board &newboard = std::get<departing_entry>(locs.back()).b;
            newboard.set_piece(p.xy(), NO_PIECE);
            dprint(entry_index, "depart", p);
            if(has_physical_check(newboard, player))
            {
                locs.pop_back();
            }
            else
            {
                jump_indices.push_back(jump_index{p, entry_index});
            }
        }
        if(arriving_moves)
        {
            for(full_move m : *arriving_moves)
            {
                // only store (possible) non-branching jump arrives
                auto [last_t, last_c] = s.get_timeline_end(m.to.l()).get();
                if(m.to.t() == last_t && player == last_c)
                {
                    if(options.prune_optional_moves && axis >= info.mandatory_lines.size()
                       && info.line_to_axis_unsafe(m.from.l()) >= info.mandatory_lines.size())
                        continue;
                    assert(m.from.tl()!=m.to.tl());
                    // store the arriving board after move is made
                    vec4 p = m.from, q = m.to;
                    piece_t pic = s.get_piece(p, player);
                    if (to_white(piece_name(pic)) == BRAWN_W && (q.y() == (player ? 0 : size_y - 1))) {
                        const piece_t promotion = ext_move(m,s).promote_to;
                        if (promotion != NO_PIECE) pic = player ? to_black(promotion) : promotion;
                    }
                    const board &destination_board = *s.get_board_ptr(q.l(), q.t(), player);

                    dprint(" ... nonbranching jump");
                    dprint(locs.size(), "arrive", m);
                    locs.emplace_back(
                        std::in_place_type<arriving_entry>, m, destination_board,
                        std::numeric_limits<index_t>::max());
                    board &newboard = std::get<arriving_entry>(locs.back()).b;
                    newboard.set_piece(q.xy(), pic);

                    // use a temporary idx of -1, will be filled later
                    if(has_physical_check(newboard, player))
                    {
                        locs.pop_back();
                    }
                }
            }
        }
        dprint("above in axis", info.line_to_axis_unsafe(l));
    }
    
    info.new_axis = info.dimension();

    // build branching axes
    // collect all branching moves
    std::vector<entry> locs;
    size_t arrival_count = 0;
    for(const auto &bucket : arrivals) arrival_count += bucket.moves.size();
    locs.reserve(1 + arrival_count);
    locs.push_back(null_entry{});
    for(const auto &bucket : arrivals)
    {
        for(full_move m : bucket.moves)
        {
            vec4 p = m.from, q = m.to;
            piece_t pic = s.get_piece(p, player);
            if (to_white(piece_name(pic)) == BRAWN_W && (q.y() == (player ? 0 : size_y - 1))) {
                const piece_t promotion = ext_move(m,s).promote_to;
                if (promotion != NO_PIECE) pic = player ? to_black(promotion) : promotion;
            }
            const board &destination_board = *s.get_board_ptr(q.l(), q.t(), player);
            
            dprint(" ... branching jump");
            if(const index_t *departure = find_jump_index(m.from))
            {
                /* only add this arriving move when the corresponding departing move
                 is legal (Otherwise, it shouldn't have been registered in jump_map) */
                dprint(locs.size(), "branching:", m);
                locs.emplace_back(
                    std::in_place_type<arriving_entry>, m, destination_board, *departure);
                board &newboard = std::get<arriving_entry>(locs.back()).b;
                newboard.set_piece(q.xy(), pic);
                if(has_physical_check(newboard, player))
                {
                    locs.pop_back();
                }
            }
        }
    }
    // replicate this axis max_branch times
    const int new_l = s.new_line();
    const int sign = signum(s.new_line()); // sign for the new lines
    for(index_t i = 0; i < max_branch; i++)
    {
        const int line = new_l + sign * static_cast<int>(i);
        assert(info.line_axes[line - info.l0_offset] == no_axis);
        info.line_axes[line - info.l0_offset] = info.new_axis + i;
        info.axis_coords.push_back(locs);
    }
    const index_t dimension = info.dimension();
    
    // build the whole space
    info.universe = HC(std::vector<integer_set>(dimension));
    info.nonjump_entries = HC(std::vector<integer_set>(dimension));
    info.departing_entries = HC(std::vector<integer_set>(dimension));
    info.arriving_entries = HC(std::vector<integer_set>(dimension));
    for(index_t n = 0; n < dimension; n++)
    {
        auto &coords = info.universe[n];
        // on nth dimension, the hypercube has coordinates 0, 1, ..., m avialible
        // which corresponds to info.axis_coords[n][0], info.axis_coords[n][1], ...
        for(index_t i = 0; i < static_cast<index_t>(info.axis_coords[n].size()); i++)
        {
            coords.insert(i);
        }
    }
    
    // fill the idx of arriving moves
    for(index_t n = 0; n < static_cast<index_t>(info.axis_coords.size()); n++)
    {
        assert(std::holds_alternative<null_entry>(info.axis_coords[n][null_coordinate]));
        for(index_t i = 0; i < static_cast<index_t>(info.axis_coords[n].size()); i++)
        {
            entry &loc = info.axis_coords[n][i];
            if(auto *p = std::get_if<arriving_entry>(&loc))
            {
                if(const index_t *departure = find_jump_index(p->m.from))
                {
                    p->idx = *departure;
#ifndef NDEBUG
                    index_t nfrom = info.line_to_axis_unsafe(p->m.from.l());
                    assert(nfrom != no_axis);
                    assert(p->m.from == std::get<departing_entry>(info.axis_coords[nfrom][p->idx]).from);
#endif
                    info.arriving_entries[n].insert(i);
                }
                else
                {
                    dprint("ghost arrive at axis:", n, ";no:", i,";move pruned:", p->m);
                    [[maybe_unused]] auto flag = info.universe[n].erase(i);
                    assert(flag);
                }
            }
            else if(std::holds_alternative<departing_entry>(loc))
            {
                info.departing_entries[n].insert(i);
            }
            else
            {
                info.nonjump_entries[n].insert(i);
            }
        }
    }
    
#ifdef DEBUGMSG
    for(size_t offset = 0; offset < info.line_axes.size(); ++offset)
    {
        const index_t axis = info.line_axes[offset];
        if(axis == no_axis) continue;
        dprint("line", info.l0_offset + static_cast<int>(offset), "=>", "axis", axis);
    }
#endif
    
    if(options.remove_check) info.prune_checks();
    return info;
}

std::pair<HC_info, search_space> HC_info::build_HC(const state &s, HC_build_options options)
{
    auto info = build_info(s, options);
    const index_t dimension = info.dimension();
    // split the search space by number of branches
    HC hc_n_lines = info.universe;
    for(index_t n = info.new_axis; n < dimension; n++)
        hc_n_lines[n] &= integer_set{null_coordinate};
    search_space ss{{hc_n_lines}};
    for(index_t n = info.new_axis; n < dimension; n++)
    {
        // Use each axis's pruned universe: check removal can differ by axis.
        hc_n_lines[n] = info.universe[n];
        hc_n_lines[n].erase(null_coordinate);
        ss.push_front(hc_n_lines); // prefer fewer branching moves
    }
    return {std::move(info), std::move(ss)};
}


void HC_info::prune_checks()
{
    const bool player = s.get_present().c();
    check_position checks(s, l0_offset,
        l0_offset + static_cast<int>(line_axes.size()) - 1);
    // In this view every existing board is immutable history. Missing future
    // boards are left absent, so a discovered attack cannot rely on assumptions
    // about another axis's move. A base-only attack excludes every completion.
    if(checks.first_check(!player, false))
    {
        if(dimension() > 0) universe[0].clear();
        return;
    }
    std::vector<int> axis_lines(dimension());
    for(std::size_t offset = 0; offset < line_axes.size(); ++offset)
        if(line_axes[offset] != no_axis)
            axis_lines[line_axes[offset]] = l0_offset + static_cast<int>(offset);

    enum class check_result { unknown, clear, checking };
    struct departure_support
    {
        check_result check = check_result::unknown;
        bool has_arrival = false;
    };
    std::vector<std::vector<departure_support>> departures(new_axis);
    for(index_t n = 0; n < new_axis; ++n)
    {
        // Arrival-heavy axes can have far more entries than departures. Only
        // allocate through the last departure ID, retaining direct indexing.
        std::size_t extent = 0;
        for(index_t i : departing_entries[n]) extent = i + 1;
        departures[n].resize(extent);
    }
    const auto overlay = [&](index_t axis, const entry &loc) {
        const auto [old_t, old_l] = extract_tl(loc);
        (void)old_l;
        const turn at = turn{old_t, player}.next_turn();
        return check_position::board_overlay{
            axis_lines[axis], at.t(), at.c(), &extract_board(loc)};
    };
    for(index_t n = 0; n < dimension(); ++n)
    {
        integer_set rejected;
        for(index_t i : universe[n])
        {
            if(i == null_coordinate) continue;
            const entry &loc = axis_coords[n][i];
            if(std::holds_alternative<departing_entry>(loc)) continue;
            const auto result = overlay(n, loc);
            if(const auto *arrival = std::get_if<arriving_entry>(&loc))
            {
                const index_t from = line_to_axis_unsafe(arrival->m.from.l());
                auto &support = departures[from][arrival->idx];
                if(support.check == check_result::checking)
                {
                    // Adding an arrival cannot repair an attack already
                    // established using the departure and immutable history.
                    rejected.insert(i);
                    continue;
                }
                const auto departure = overlay(from, axis_coords[from][arrival->idx]);
                // This arrival forces both boards. A surviving pair also proves
                // that its departure alone cannot create an unavoidable check.
                if(!checks.first_check(!player, result, departure, false))
                {
                    support.check = check_result::clear;
                    support.has_arrival = true;
                    continue;
                }
                rejected.insert(i);
                if(support.check == check_result::unknown)
                    support.check = checks.first_check(!player, departure, std::nullopt, false)
                        ? check_result::checking : check_result::clear;
            }
            else if(checks.first_check(!player, result, std::nullopt, false))
                rejected.insert(i);
        }
        universe[n].minus(rejected);
        nonjump_entries[n].minus(rejected);
        arriving_entries[n].minus(rejected);
    }
    // A departure cannot occur without an arrival. Support includes arrivals on
    // every axis, including copies on prospective branching lines. Entry tables
    // stay untouched because arrivals refer to their original coordinate IDs.
    for(index_t n = 0; n < new_axis; ++n)
    {
        integer_set unsupported;
        for(index_t i : departing_entries[n])
            if(!departures[n][i].has_arrival) unsupported.insert(i);
        universe[n].minus(unsupported);
        departing_entries[n].minus(unsupported);
    }
}

std::tuple<HC_info, HC, std::vector<slice>> HC_info::build_solid_HC(const state &s, HC_build_options options)
{
    auto info = build_info(s, options);
    std::vector<slice> negative;
    for(index_t n = info.new_axis; n + 1 < info.dimension(); ++n)
    {
        if(!info.universe[n].contains(null_coordinate)) continue;
        integer_set occupied = info.universe[n + 1];
        occupied.erase(null_coordinate);
        if(occupied.empty()) continue;
        slice gap;
        gap.fix_axis(n, integer_set{null_coordinate});
        gap.fix_axis(n + 1, std::move(occupied));
        negative.push_back(std::move(gap));
    }
    HC domain = info.universe;
    return {std::move(info), std::move(domain), std::move(negative)};
}


std::unique_ptr<HC_info::local_space>
HC_info::build_local_space(const HC &hc, const point &seed) const
{
    std::unique_ptr<local_space> result;
    std::vector<integer_set> coordinates;
    std::vector<std::pair<index_t, integer_set>> departure_domains;
    for(index_t n = 0; n < dimension(); ++n)
    {
        integer_set alternatives;
        integer_set departure_values;
        index_t departure_axis = no_axis;
        if(seed[n] != null_coordinate && nonjump_entries[n].contains(seed[n]))
        {
            alternatives = hc[n] & nonjump_entries[n];
            alternatives.erase(null_coordinate);
        }
        else if(arriving_entries[n].contains(seed[n]))
        {
            const auto &anchor = std::get<arriving_entry>(axis_coords[n][seed[n]]);
            departure_axis = line_to_axis_unsafe(anchor.m.from.l());
            assert(departure_axis != no_axis && seed[departure_axis] == anchor.idx);
            arriving_entries[n].for_each_intersection(hc[n], [&](index_t i) {
                const auto &arrival = std::get<arriving_entry>(axis_coords[n][i]);
                if(arrival.m.from.tl() == anchor.m.from.tl()
                    && arrival.m.to.tl() == anchor.m.to.tl()
                    && hc[departure_axis].contains(arrival.idx))
                {
                    alternatives.insert(i);
                    departure_values.insert(arrival.idx);
                }
            });
        }
        if(alternatives.size() <= 1) continue;
        if(!result) result = std::make_unique<local_space>();
        if(departure_values.size() > 1)
        {
            assert(std::none_of(departure_domains.begin(), departure_domains.end(),
                [&](const auto &p) { return p.first == departure_axis; }));
            departure_domains.emplace_back(departure_axis, std::move(departure_values));
        }
        result->axes.push_back(n);
        result->seed.push_back(seed[n]);
        coordinates.push_back(std::move(alternatives));
    }
    if(!result) return nullptr;
    result->universe = HC(std::move(coordinates));
    // The outer rectangle includes mismatched departure/arrival combinations.
    // Every matching-valid point in it uses exactly the seed's fixed axis pairs
    // and is represented by a local arrival together with its cached departure.
    std::size_t local_axis = 0;
    for(index_t n = 0; n < dimension(); ++n)
    {
        if(local_axis < result->axes.size() && result->axes[local_axis] == n)
        {
            const auto &values = result->universe[local_axis++];
            if(!hc[n].is_subset_of(values))
                result->outer_region.fix_axis(n, values);
        }
        else
        {
            const auto departure = std::find_if(departure_domains.begin(), departure_domains.end(),
                [&](const auto &p) { return p.first == n; });
            if(departure != departure_domains.end())
            {
                if(!hc[n].is_subset_of(departure->second))
                    result->outer_region.fix_axis(n, departure->second);
            }
            else if(hc[n].size() != 1)
            {
                result->outer_region.fix_axis(n, {seed[n]});
            }
        }
    }
    return result;
}

bool HC_info::prune_retained_slices(HC &hc, std::span<const slice> negative)
{
    if(hc.empty()) return false;
    bool changed;
    do
    {
        changed = false;
        for(const slice &problem : negative)
        {
            if(!hc.is_slice_good(problem)) continue;
            const auto &fixed = problem.get_fixed_axes();
            const auto cut = std::find_if(fixed.begin(), fixed.end(),
                [&](const auto &axis) {
                    return !hc[axis.first].is_subset_of(axis.second);
                });
            if(cut == fixed.end()) return false; // The slice covers this HC.
            hc[cut->first].minus(cut->second);
            changed = true;
        }
        // A cut can make an earlier constraint unary or cover the whole HC.
    } while(changed);
    return true;
}


std::optional<slice> HC_info::find_problem(const point &p, const HC &hc) const
{
    std::optional<slice> problem = jump_order_consistent(p, hc).or_else([this, &hc, &p]() {
        return test_present(p, hc).or_else([this, &hc, &p]() {
            return find_checks(p, hc);
        });
    });
    return problem;
}

std::optional<slice> HC_info::jump_order_consistent(const point &p, const HC &hc) const
{
    dprint("jump_order_consistent()");
    /* throughout the search, maintain the jump_map of (l, t) => new_l
    so p[new_l] is a arriving move from (l0, t0) that jumps to (l, t) which create a branch
    remember t, l are stored as the higher part of a vec4
     */
    for(index_t n = new_axis; n + 1 < dimension(); ++n)
    {
        if(p[n] != null_coordinate || p[n + 1] == null_coordinate) continue;
        integer_set occupied = hc[n + 1];
        occupied.erase(null_coordinate);
        slice problem;
        problem.fix_axis(n, integer_set{null_coordinate});
        problem.fix_axis(n + 1, std::move(occupied));
        return problem;
    }
    std::map<vec4, index_t> jump_map;
    const bool c = s.get_present().c();
    
    for(index_t n = new_axis; n < dimension(); n++)
    {
        // within this loop, n is the axis for new_l
        const index_t in = p[n];
        const entry &loc = axis_coords[n][in];
        if(in == null_coordinate)
        {
            break;
            // Gaps were rejected above; occupied branch axes form a prefix.
        }
        assert(std::holds_alternative<arriving_entry>(loc));
        const arriving_entry arr = std::get<arriving_entry>(loc);
        const vec4 from = arr.m.from, to = arr.m.to;
        /* case one: there is a branching move (l0,t0) >> (l',t') ~> new_l, but
         (l',t') is a playable board in which the corresponding played move
         is a pass */
        if(const index_t m = line_to_axis_unsafe(to.l()); m != no_axis)
        {
            const index_t im = p[m];
            if(im == null_coordinate
               && s.get_timeline_end(to.l()) == turn{to.t(), c})
            {
                /* ban these combinations:
                 -[m,xm] the null_entry on (l', t'); with
                 -[s] any branching move on axis n to (l', t') (which is a null_entry)
                 i.e. all moves >> (l',t') then creates branch new_l
                 */
                integer_set s;
                arriving_entries[n].for_each_intersection(hc[n], [&](index_t i) {
                    vec4 to3 = std::get<arriving_entry>(axis_coords[n][i]).m.to;
                    if(to3.tl() == to.tl()) s.insert(i);
                });
                std::map<index_t, integer_set> fixed_axes {{n, s}, {m, integer_set{im}}};
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
            index_t axis_branch = jump_map[critical_tl];
            integer_set s1, s2;
            arriving_entries[n].for_each_intersection(hc[n], [&](index_t i) {
                vec4 from1 = std::get<arriving_entry>(axis_coords[n][i]).m.from;
                if(from1.tl() == critical_tl) s1.insert(i);
            });
            arriving_entries[axis_branch].for_each_intersection(hc[axis_branch], [&](index_t i) {
                vec4 to2 = std::get<arriving_entry>(axis_coords[axis_branch][i]).m.to;
                if(to2.tl() == critical_tl)
                {
                    s2.insert(i);
                }
            });
            std::map<index_t, integer_set> fixed_axes {{n, s1}, {axis_branch, s2}};
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

std::optional<slice> HC_info::test_present(const point &p, const HC &hc) const
{
    const bool c = s.get_present().c();
    const auto [l0_min, l0_max] = s.get_initial_lines_range();
    const auto [l_min, l_max] = s.get_lines_range();
    index_t branches = 0;
    while(new_axis + branches < dimension()
          && p[new_axis + branches] != null_coordinate)
    {
        ++branches;
    }
    const auto active_range = [&](index_t count) {
        const int lo = l_min - (c ? static_cast<int>(count) : 0);
        const int hi = l_max + (c ? 0 : static_cast<int>(count));
        // Extents from the initial range handle both odd and even layouts,
        // matching multiverse::calculate_active_range().
        const int radius = std::min(l0_min - lo, hi - l0_max) + 1;
        return std::pair{std::max(lo, l0_min - radius), std::min(hi, l0_max + radius)};
    };
    const auto [active_min, active_max] = active_range(branches);
    const auto branch_line = [&](index_t n) {
        const int offset = static_cast<int>(n - new_axis) + 1;
        return c ? l_min - offset : l_max + offset;
    };
    const auto branch_end = [&](const entry &loc) {
        return turn{std::get<arriving_entry>(loc).m.to.t(), c}.next_turn();
    };

    turn present = turn::max;
    std::optional<index_t> pass_axis;
    int witness_line = 0;
    for(int l = std::max(l_min, active_min); l <= std::min(l_max, active_max); ++l)
    {
        turn end = s.get_timeline_end(l);
        const index_t axis = line_to_axis_unsafe(l);
        const bool moved = axis != no_axis && p[axis] != null_coordinate;
        if(moved) end = end.next_turn();
        if(end <= present)
        {
            witness_line = l;
            present = end;
            pass_axis = axis != no_axis && !moved
                ? std::optional<index_t>{axis} : std::nullopt;
        }
    }
    for(index_t n = new_axis; n < new_axis + branches; ++n)
    {
        const int l = branch_line(n);
        if(l < active_min || l > active_max) continue;
        const turn end = branch_end(axis_coords[n][p[n]]);
        if(end < present)
        {
            present = end;
            pass_axis = std::nullopt;
        }
    }
    if(present.c() != c)
    {
        return std::nullopt;
    }

    // A current-player endpoint must be an unchanged playable board.
    assert(pass_axis);
    slice problem;
    problem.fix_axis(*pass_axis, integer_set{p[*pass_axis]});
    // Other playable endpoints already start at or after this current-player
    // present: advancing one half-turn cannot cross another current-player
    // endpoint. Their move/pass choices therefore need no restriction.
    // Bound branch counts by the witness's activation and the first possible
    // activation of an earlier existing endpoint. The valid counts form an
    // interval because active ranges only grow as branches are added.
    index_t min_branches = branches, max_branches = branches;
    int widest_min = active_min, widest_max = active_max;
    for(index_t count = 0; count <= dimension() - new_axis; ++count)
    {
        const auto [active_lo, active_hi] = active_range(count);
        if(witness_line < active_lo || witness_line > active_hi) continue;
        bool safe = true;
        for(int l = std::max(l_min, active_lo); l <= std::min(l_max, active_hi); ++l)
            if(s.get_timeline_end(l) < present) safe = false;
        if(!safe) continue;
        min_branches = std::min(min_branches, count);
        max_branches = std::max(max_branches, count);
        widest_min = std::min(widest_min, active_lo);
        widest_max = std::max(widest_max, active_hi);
    }
    for(index_t n = new_axis; n < dimension(); ++n)
    {
        const index_t offset = n - new_axis;
        const int l = branch_line(n);
        integer_set allowed;
        // Identify genuine restrictions against the universe, then clip each
        // restricted axis to hc to preserve the remove_slice() contract.
        if(min_branches == 0 || offset != min_branches - 1)
            allowed.insert(null_coordinate);
        if(offset != max_branches)
        {
            if(l < widest_min || l > widest_max)
            {
                allowed |= arriving_entries[n];
            }
            else
            {
                for(index_t i : arriving_entries[n])
                    if(branch_end(axis_coords[n][i]) >= present) allowed.insert(i);
            }
        }
        // Branches form a prefix, so only the two boundary axes are needed
        // to preserve the count interval. Omit every unrestricted axis.
        if(!universe[n].is_subset_of(allowed))
        {
            allowed &= hc[n];
            problem.fix_axis(n, allowed);
        }
    }
    assert(problem.contains(p));
    return problem;
}

std::optional<slice> HC_info::find_checks(const point &p, const HC &hc) const
{
    dprint("HC_info::find_checks()");
    const bool c = s.get_present().c();
    check_position newstate(s, l0_offset,
        l0_offset + static_cast<int>(line_axes.size()) - 1);
    for(size_t offset = 0; offset < line_axes.size(); ++offset) {
        const index_t axis = line_axes[offset];
        if(axis == no_axis) continue;
        const int l = l0_offset + static_cast<int>(offset);
        const entry &loc = axis_coords[axis][p[axis]];
        if(p[axis] == null_coordinate) continue;
        const auto [old_t, old_l] = extract_tl(loc);
        (void)old_l;
        // Branch arrivals use the axis's new line, not their old hotspot line.
        newstate.add_board(l, turn{old_t,c}.next_turn(), extract_board(loc));
    }
    // HC construction has already filtered physical checks on resulting boards.
    if(auto maybe_check = newstate.first_check(!c, false))
    {
        const auto for_each_board = [&](index_t axis, auto &&visit) {
            nonjump_entries[axis].for_each_intersection(hc[axis], [&](index_t i) {
                if(i == null_coordinate) return;
                const auto &loc = std::get<physical_entry>(axis_coords[axis][i]);
                visit(i, loc.m.from.t(), loc.b);
            });
            departing_entries[axis].for_each_intersection(hc[axis], [&](index_t i) {
                const auto &loc = std::get<departing_entry>(axis_coords[axis][i]);
                visit(i, loc.from.t(), loc.b);
            });
            arriving_entries[axis].for_each_intersection(hc[axis], [&](index_t i) {
                const auto &loc = std::get<arriving_entry>(axis_coords[axis][i]);
                visit(i, loc.m.to.t(), loc.b);
            });
        };
        // there is a check
        // the slice to remove is a product of coordinates on certain axes
        full_move check = maybe_check.value();
        dprint("found check: ", check);
        assert(check.from.tl() != check.to.tl() && "physical checks should have already removed");
        auto [path, sliding_type] = get_move_path(newstate, check, !c);
        auto is_next = c ? [](int t1, int t2){
            return t1 + 1 == t2;
        }:[](int t1, int t2){
            return t1 == t2;
        };
        /* on axis for check.from.l():
         ban all moves that allows the same piece hostile piece remain in that position
         */
        slice problem;
        if(const index_t n1 = line_to_axis_unsafe(check.from.l()); n1 != no_axis)
        {
            integer_set not_taking;
            for_each_board(n1, [&](index_t i, int old_t, const board &newboard) {
                /* if there isn't a new board on the same place, it won't create the same check*/
                if(!is_next(old_t, check.from.t()))
                {
                    return;
                }

                if(sliding_type)
                {
                    bitboard_t bb = c ? newboard.white() : newboard.black();
                    switch(sliding_type)
                    {
                        case 1:
                            bb &= newboard.lrook();
                            break;
                        case 2:
                            bb &= newboard.lbishop();
                            break;
                        case 3:
                            bb &= newboard.lunicorn();
                            break;
                        case 4:
                            bb &= newboard.ldragon();
                            break;
                        default:
                            assert(false && "wrong sliding type infered");
                    }
                    if(pmask(check.from.xy()) & bb)
                    {
                        dprint(n1, i, sliding_type, to_semimove(axis_coords[n1][i]).lan(s));
                        dprint("axis", n1, "not taking (sliding)", i);
                        not_taking.insert(i);
                    }
                }
                else if(newboard.get_piece(check.from.xy()) == newstate.get_piece(check.from, !c))
                {
                    // non sliding pieces remains in same position
                    dprint(n1, i, sliding_type, to_semimove(axis_coords[n1][i]).lan(s));
                    dprint("axis", n1, "not taking (untouched)", i);
                    not_taking.insert(i);
                }
            });
            problem.fix_axis(n1, not_taking);
        }
        /* on axis for check.to.l():
         if the board of checks.to is what just played, ban all moves that leave
         a royal piece on check.to.
         Otherwise, don't alter that axis futher.
         */
        int l2 = check.to.l();
        if(const index_t n2 = line_to_axis_unsafe(l2); n2 != no_axis)
        {
            const entry &loc0 = axis_coords[n2][p[n2]];
            if(p[n2] == null_coordinate || !is_next(extract_tl(loc0).first, check.to.t()))
            {
                // pass to ban everything
            }
            else
            {
                integer_set expose_royal;
                for_each_board(n2, [&](index_t i, int old_t, const board &newboard) {
                    /* if there isn't a new board on the same place, do nothing*/
                    if(!is_next(old_t, check.to.t()))
                    {
                        return;
                    }

                    bitboard_t friendly = c ? newboard.black() : newboard.white();
                    bool is_royal = pmask(check.to.xy()) & newboard.royal() & friendly;
                    if(is_royal)
                    {
                        dprint(to_semimove(axis_coords[n2][i]).lan(s));
                        dprint("axis", n2, "expose royal", i);
                        expose_royal.insert(i);
                    }
                });
                problem.fix_axis(n2, expose_royal);
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
            if(const index_t n = line_to_axis_unsafe(crossed.l()); n != no_axis)
            {
                const entry &loc0 = axis_coords[n][p[n]];
                if(p[n] == null_coordinate || !is_next(extract_tl(loc0).first, crossed.t()))
                {
                    // pass to ban everything
                }
                else
                {
                    bitboard_t z = pmask(crossed.xy());
                    integer_set not_blocking;
                    for_each_board(n, [&](index_t i, int old_t, const board &newboard) {
                        /* if there isn't a board, then nothing pass through it*/
                        if(!is_next(old_t, crossed.t()))
                        {
                            return;
                        }

                        /* if the very place is empty, then it is clearly not blocking*/
                        if(!(z & newboard.occupied()))
                        {
                            dprint(n, i, sliding_type, to_semimove(axis_coords[n][i]).lan(s));
                            dprint("axis", n, "not blocking (empty)", i);
                            not_blocking.insert(i);
                            return;
                        }
                        /* on the crossed point, if a hostile piece with the same sliding type
                         of the attacking piece is placed here, it doesn't completely resolve the check
                         */
                        if(sliding_type)
                        {
                            bitboard_t bb = c ? newboard.white() : newboard.black();
                            switch(sliding_type)
                            {
                                case 1:
                                    bb &= newboard.lrook();
                                    break;
                                case 2:
                                    bb &= newboard.lbishop();
                                    break;
                                case 3:
                                    bb &= newboard.lunicorn();
                                    break;
                                case 4:
                                    bb &= newboard.ldragon();
                                    break;
                                default:
                                    assert(false && "wrong sliding type infered");
                            }
                            if(z & bb)
                            {
                                dprint(n, i, sliding_type, to_semimove(axis_coords[n][i]).lan(s));
                                dprint("axis", n, "not blocking (sliding)", i);
                                not_blocking.insert(i);
                                return;
                            }
                        }
                        /* on the crossed point, if a friendly royal piece is there
                         it is still checking despite the path is technically blocked
                         */
                        bitboard_t friendly = c ? newboard.black() : newboard.white();
                        if(z & newboard.royal() & friendly)
                        {
                            dprint(n, i, sliding_type, to_semimove(axis_coords[n][i]).lan(s));
                            dprint("axis", n, "not blocking (expose royal)", i);
                            not_blocking.insert(i);
                            return;
                        }
                    });
                    problem.fix_axis(n, not_blocking);
                }
            }
        }
        dprint("point:", range_to_string(p));
        dprint("problem", problem.to_string());
        assert(problem.contains(p));
        return problem;
    }
    dprint("no checks found");
    return std::nullopt;
}

// ------------------------------------------------------------


// /* for debugging only (this version is more friendly with stacktracing) */
//std::vector<moveseq> HC_info::search1(search_space ss) const
//{
//    std::vector<moveseq> result;
//    adprint("begining search: ", ss.to_string());
//    while(!ss.empty())
//    {
//        HC hc = ss.back();
//        adprint("searching ", hc.to_string());
//        ss.pop_back();
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


moveseq HC_info::to_action(const point &p) const
{
    std::vector<full_move> mvs;
    for(const index_t axis : line_axes)
    {
        if(axis == no_axis) continue;
        const entry &loc = axis_coords[axis][p[axis]];
        if(std::holds_alternative<physical_entry>(loc))
        {
            mvs.push_back(std::get<physical_entry>(loc).m);
        }
        else if(std::holds_alternative<arriving_entry>(loc))
        {
            mvs.push_back(std::get<arriving_entry>(loc).m);
        }
    }
    const bool c = s.get_present().c();
    if(c)
    {
        std::reverse(mvs.begin(), mvs.end());
    }
#ifdef DEBUGMSG
    dprint("HC_info::to_action: returning", mvs.size(), "moves:");
    for(const auto &mv : mvs)
    {
        dprint(" ", mv.to_string());
    }
#endif
    return mvs;
}


bool HC_info::prune_slice_refs(HC &domain, std::span<const slice *const> negative)
{
    if(domain.empty()) return false;
    bool changed;
    do
    {
        changed = false;
        for(const auto *problem : negative)
        {
            if(!domain.is_slice_good(*problem)) continue;
            const auto &fixed = problem->get_fixed_axes();
            const auto axis = std::find_if(fixed.begin(), fixed.end(), [&](const auto &entry) {
                return !domain[entry.first].is_subset_of(entry.second);
            });
            if(axis == fixed.end()) return false;
            domain[axis->first].minus(axis->second);
            changed = true;
        }
    } while(changed);
    return true;
}


std::optional<point> HC_info::try_local_completion(
    const point &seed, index_t axis, index_t coordinate) const
{
    const auto &old_entry = axis_coords[axis][seed[axis]];
    const auto &new_entry = axis_coords[axis][coordinate];
    const auto *old_arrival = std::get_if<arriving_entry>(&old_entry);
    const auto *new_arrival = std::get_if<arriving_entry>(&new_entry);
    index_t source = no_axis;
    if(old_arrival && new_arrival)
    {
        // The local class fixes the jump endpoints in time and on timelines;
        // its arrival coordinates may vary together with their departures.
        if(old_arrival->m.from.tl() != new_arrival->m.from.tl()
           || old_arrival->m.to.tl() != new_arrival->m.to.tl()) return std::nullopt;
        source = line_to_axis_unsafe(old_arrival->m.from.l());
        if(source == no_axis || seed[source] != old_arrival->idx
           || (source < axis && seed[source] != new_arrival->idx)) return std::nullopt;
    }
    else if(!std::holds_alternative<physical_entry>(old_entry)
            || !std::holds_alternative<physical_entry>(new_entry)) return std::nullopt;
    point candidate = seed;
    candidate[axis] = coordinate;
    if(source != no_axis) candidate[source] = new_arrival->idx;
    if(find_checks(candidate, universe)) return std::nullopt;
    return candidate;
}

std::optional<point> HC_info::try_physical_completion(
    const point &seed, index_t axis, index_t coordinate) const
{
    if(!std::holds_alternative<physical_entry>(axis_coords[axis][seed[axis]])
       || !std::holds_alternative<physical_entry>(axis_coords[axis][coordinate])) return std::nullopt;
    return try_local_completion(seed, axis, coordinate);
}

integer_set HC_info::same_move_kind(index_t axis, index_t coordinate, const integer_set &allowed) const
{
    if(nonjump_entries[axis].contains(coordinate)) return allowed & nonjump_entries[axis];
    if(departing_entries[axis].contains(coordinate)) return allowed & departing_entries[axis];
    return allowed & arriving_entries[axis];
}


std::shared_ptr<const HC_info::constraint_index> HC_info::build_constraint_index() const
{
    auto result = std::make_shared<constraint_index>();
    result->destinations.resize(dimension());
    result->targets.resize(dimension());
    result->departures.resize(dimension());
    for(index_t n = 0; n < dimension(); ++n)
    {
        auto &destination = result->destinations[n];
        destination.entries.resize(axis_coords[n].size());
        for(auto i : arriving_entries[n])
        {
            const auto &arrival = std::get<arriving_entry>(axis_coords[n][i]);
            const auto source = line_to_axis_unsafe(arrival.m.from.l());
            auto found = std::find(destination.sources.begin(), destination.sources.end(), source);
            if(found == destination.sources.end())
            {
                destination.sources.push_back(source);
                result->targets[source].push_back(n);
                found = destination.sources.end() - 1;
            }
            destination.entries[i] = {source, arrival.idx, index_t(found - destination.sources.begin())};
            auto &targets = result->departures[source][arrival.idx];
            if(targets.empty() || targets.back().first != n) targets.emplace_back(n, integer_set{});
            targets.back().second.insert(i);
        }
    }
    return result;
}

bool HC_info::constrain_domain(HC &domain, constraint_workspace *scratch) const
{
    assert(domain.dimension() == dimension());
    for(index_t n = 0; n < dimension(); ++n) domain[n] &= universe[n];
    constraint_workspace local;
    auto &storage = scratch ? *scratch : local;
    auto &paired = storage.paired;
    auto &supported_departures = storage.supported_departures;
    auto &minimum = storage.minimum;
    auto &maximum = storage.maximum;
    paired.assign(dimension(), no_axis);
    supported_departures.resize(dimension());
    minimum.resize(new_axis);
    maximum.resize(new_axis);
    const constraint_index *index = nullptr;
    // Tiny/one-shot queries keep the allocation-free-index path. Repeated
    // queries amortize the immutable incidence index and per-destination caches.
    if(scratch && storage.retain_domains && ++storage.calls >= 3)
    {
        if(!constraints) constraints = build_constraint_index();
        if(storage.index != constraints)
        {
            storage.index = constraints;
            storage.support_domain = HC(std::vector<integer_set>(dimension()));
            storage.bound_domain = HC(std::vector<integer_set>(new_axis));
            storage.support_pairs.assign(dimension(), no_axis);
            storage.supports.clear();
            storage.supports.resize(dimension());
            storage.active_supports.clear();
            storage.active_supports.resize(dimension());
            storage.dirty.resize(dimension());
            for(index_t n = 0; n < dimension(); ++n)
                storage.supports[n].resize(constraints->destinations[n].sources.size());
            std::fill(minimum.begin(), minimum.end(), 0);
            std::fill(maximum.begin(), maximum.end(), 0);
            storage.lower = storage.upper = 0;
        }
        index = storage.index.get();
    }
    const auto equal = [](const integer_set &a, const integer_set &b) {
        return a.is_subset_of(b) && b.is_subset_of(a);
    };
    bool changed;
    do
    {
        if(domain.empty()) return false;
        changed = false;
        const auto restrict_axis = [&](index_t axis, const integer_set &allowed) {
            const auto previous = domain[axis].size();
            domain[axis] &= allowed;
            changed |= domain[axis].size() != previous;
        };
        // Every existing departure is paired with either an existing arrival
        // or a branch arrival. Propagate bounds on that conserved count before
        // scanning potentially large arrival domains.
        int lower = 0, upper = 0;
        for(index_t n = 0; n < new_axis; ++n)
        {
            if(!index || !equal(storage.bound_domain[n], domain[n]))
            {
                if(index) { storage.lower -= minimum[n]; storage.upper -= maximum[n]; }
                const bool arrives = domain[n].intersects(arriving_entries[n]);
                const bool departs = domain[n].intersects(departing_entries[n]);
                const bool nonjump = domain[n].intersects(nonjump_entries[n]);
                minimum[n] = arrives ? -1 : nonjump ? 0 : 1;
                maximum[n] = departs ? 1 : nonjump ? 0 : -1;
                if(index)
                {
                    storage.bound_domain[n] = domain[n];
                    storage.lower += minimum[n]; storage.upper += maximum[n];
                }
            }
            if(!index) { lower += minimum[n]; upper += maximum[n]; }
        }
        if(index) { lower = storage.lower; upper = storage.upper; }
        int branch_min = 0, branch_max = dimension() - new_axis;
        for(index_t n = new_axis; n < dimension(); ++n)
        {
            if(!domain[n].contains(null_coordinate)) branch_min = n - new_axis + 1;
            if(domain[n].size() == 1 && domain[n].contains(null_coordinate))
                branch_max = std::min(branch_max, int(n - new_axis));
        }
        if(lower > branch_max || upper < branch_min) return false;
        for(index_t n = 0; n < new_axis; ++n)
        {
            integer_set unsupported;
            const auto impossible = [&](int contribution) {
                return lower - minimum[n] + contribution > branch_max
                    || upper - maximum[n] + contribution < branch_min;
            };
            if(impossible(-1)) unsupported |= arriving_entries[n];
            if(impossible(0)) unsupported |= nonjump_entries[n];
            if(impossible(1)) unsupported |= departing_entries[n];
            if(domain[n].intersects(unsupported)) { domain[n].minus(unsupported); changed = true; }
        }
        for(index_t n = new_axis; n < dimension(); ++n)
        {
            if(int(n - new_axis) < lower && domain[n].erase(null_coordinate)) changed = true;
            if(int(n - new_axis) >= upper) restrict_axis(n, integer_set{null_coordinate});
        }
        if(domain.empty()) return false;
        std::fill(paired.begin(), paired.end(), no_axis);
        for(index_t n = 0; n < dimension(); ++n)
            if(domain[n].size() == 1)
                if(const auto *arrival = std::get_if<arriving_entry>(&axis_coords[n][*domain[n].begin()]))
                {
                    const auto source = line_to_axis_unsafe(arrival->m.from.l());
                    if(paired[source] != no_axis && paired[source] != n) return false;
                    paired[source] = n;
                    restrict_axis(source, integer_set{arrival->idx});
                }
        if(domain.empty()) return false;
        for(auto &supported : supported_departures) supported.clear();
        if(index)
        {
            std::fill(storage.dirty.begin(), storage.dirty.end(), false);
            for(index_t n = 0; n < dimension(); ++n)
            {
                const bool altered = !equal(storage.support_domain[n], domain[n]);
                if(altered) storage.dirty[n] = true;
                if(altered || paired[n] != storage.support_pairs[n])
                    for(auto target : index->targets[n]) storage.dirty[target] = true;
                if(altered) storage.support_domain[n] = domain[n];
            }
            storage.support_pairs = paired;
            for(index_t n = 0; n < dimension(); ++n)
            {
                const auto &destination = index->destinations[n];
                if(storage.dirty[n])
                {
                    for(auto slot : storage.active_supports[n]) storage.supports[n][slot].clear();
                    storage.active_supports[n].clear();
                    integer_set rejected;
                    arriving_entries[n].for_each_intersection(domain[n], [&](index_t i) {
                        const auto &arrival = destination.entries[i];
                        if(!domain[arrival.source].contains(arrival.departure)
                           || (paired[arrival.source] != no_axis && paired[arrival.source] != n)) rejected.insert(i);
                        else
                        {
                            auto &support = storage.supports[n][arrival.slot];
                            if(support.empty()) storage.active_supports[n].push_back(arrival.slot);
                            support.insert(arrival.departure);
                        }
                    });
                    if(!rejected.empty()) { domain[n].minus(rejected); changed = true; }
                }
                for(auto slot : storage.active_supports[n])
                    supported_departures[destination.sources[slot]] |= storage.supports[n][slot];
            }
        }
        else
            for(index_t n = 0; n < dimension(); ++n)
            {
                integer_set rejected;
                arriving_entries[n].for_each_intersection(domain[n], [&](index_t i) {
                    const auto &arrival = std::get<arriving_entry>(axis_coords[n][i]);
                    const auto source = line_to_axis_unsafe(arrival.m.from.l());
                    if(!domain[source].contains(arrival.idx) || (paired[source] != no_axis && paired[source] != n))
                        rejected.insert(i);
                    else supported_departures[source].insert(arrival.idx);
                });
                if(!rejected.empty()) { domain[n].minus(rejected); changed = true; }
            }
        for(index_t source = 0; source < dimension(); ++source)
        {
            auto unsupported = domain[source] & departing_entries[source];
            unsupported.minus(supported_departures[source]);
            if(!unsupported.empty()) { domain[source].minus(unsupported); changed = true; }
        }
        for(index_t source = 0; source < dimension(); ++source)
        {
            if(domain[source].size() != 1) continue;
            const auto departure = *domain[source].begin();
            if(!departing_entries[source].contains(departure)) continue;
            index_t only_axis = no_axis;
            integer_set only_values;
            bool several = false;
            if(index)
            {
                const auto found = index->departures[source].find(departure);
                if(found != index->departures[source].end())
                    for(const auto &[n, values] : found->second)
                    {
                        if(!domain[n].intersects(values)) continue;
                        if(only_axis != no_axis) { several = true; break; }
                        only_axis = n;
                        only_values = domain[n] & values;
                    }
            }
            else
                for(index_t n = 0; n < dimension(); ++n)
                {
                    integer_set supported;
                    arriving_entries[n].for_each_intersection(domain[n], [&](index_t i) {
                        const auto &arrival = std::get<arriving_entry>(axis_coords[n][i]);
                        if(line_to_axis_unsafe(arrival.m.from.l()) == source && arrival.idx == departure)
                            supported.insert(i);
                    });
                    if(supported.empty()) continue;
                    if(only_axis != no_axis) { several = true; break; }
                    only_axis = n;
                    only_values = std::move(supported);
                }
            if(only_axis == no_axis) return false;
            if(!several) restrict_axis(only_axis, only_values);
        }
        for(index_t n = new_axis; n + 1 < dimension(); ++n)
        {
            if(domain[n].size() == 1 && domain[n].contains(null_coordinate))
                restrict_axis(n + 1, integer_set{null_coordinate});
            if(!domain[n + 1].contains(null_coordinate) && domain[n].erase(null_coordinate)) changed = true;
        }
    } while(changed);
    return !domain.empty();
}


bool HC_info::local_contains(const local_space &local, const point &p)
{
    if(!local.outer_region.contains(p)) return false;
    for(std::size_t variable = 0; variable < local.axes.size(); ++variable)
        if(!local.universe[variable].contains(p[local.axes[variable]])) return false;
    return true;
}

void HC_info::prepare_local_layout(local_space &local, const point &seed) const
{
    if(local.projection) return;
    auto layout = std::make_unique<local_space::layout>();
    if(local.representative.empty()) local.representative = seed;
    layout->variables.assign(dimension(), no_axis);
    layout->departures.assign(dimension(), no_axis);
    layout->sources.assign(local.axes.size(), no_axis);
    layout->partners.resize(local.axes.size());
    for(index_t variable = 0; variable < local.axes.size(); ++variable)
    {
        const auto axis = local.axes[variable];
        layout->variables[axis] = variable;
        if(!arriving_entries[axis].contains(local.seed[variable])) continue;
        const auto &anchor = std::get<arriving_entry>(axis_coords[axis][local.seed[variable]]);
        const auto source = line_to_axis_unsafe(anchor.m.from.l());
        layout->sources[variable] = source;
        layout->departures[source] = variable;
        for(auto coordinate : local.universe[variable])
            layout->partners[variable][std::get<arriving_entry>(axis_coords[axis][coordinate]).idx].insert(coordinate);
    }
    local.projection = std::move(layout);
}

bool HC_info::restrict_local_axis(HC &projected, const local_space &local,
                                   index_t axis, index_t coordinate) const
{
    const auto &layout = *local.projection;
    if(const auto variable = layout.variables[axis]; variable != no_axis)
    {
        projected[variable] &= integer_set{coordinate};
        return !projected[variable].empty();
    }
    if(const auto variable = layout.departures[axis]; variable != no_axis)
    {
        const auto found = layout.partners[variable].find(coordinate);
        if(found == layout.partners[variable].end()) return false;
        projected[variable] &= found->second;
        return !projected[variable].empty();
    }
    return local.representative[axis] == coordinate;
}

bool HC_info::prepare_local_cursor(completion_space &space, std::unique_ptr<local_cursor> &cursor,
    const local_cursor *parent, const point &seed, index_t axis, index_t coordinate) const
{
    if(!space.reuse_local) return false;
    // A pinned class fixes move kinds and jump endpoints. Reject incompatible
    // choices before class lookup or any domain allocation.
    const auto &old_entry = axis_coords[axis][seed[axis]];
    const auto &next_entry = axis_coords[axis][coordinate];
    if(old_entry.index() != next_entry.index()) return false;
    if(const auto *arrival = std::get_if<arriving_entry>(&old_entry))
    {
        const auto &next = std::get<arriving_entry>(next_entry);
        if(arrival->m.from.tl() != next.m.from.tl() || arrival->m.to.tl() != next.m.to.tl()) return false;
        const auto source = line_to_axis_unsafe(arrival->m.from.l());
        if(source < axis && seed[source] != next.idx) return false;
    }
    if(cursor && cursor->prefix_length == axis && local_contains(*cursor->local, seed)) return true;
    std::shared_ptr<local_space> local;
    if(parent && local_contains(*parent->local, seed)) local = parent->local;
    else if(space.local && local_contains(*space.local, seed)) local = space.local;
    else
        for(const auto &cached : space.local_cache)
            if(local_contains(*cached, seed)) { local = cached; break; }
    if(!local)
    {
        local = build_local_space(universe, seed);
        if(!local) return false;
        local->representative = seed;
        if(space.local_capacity)
        {
            if(space.local_cache.size() >= space.local_capacity) space.local_cache.erase(space.local_cache.begin());
            space.local_cache.push_back(local);
        }
    }
    prepare_local_layout(*local, seed);
    auto next = std::make_unique<local_cursor>();
    next->local = std::move(local);
    const bool inherit = parent && parent->local == next->local && parent->prefix_length <= axis;
    next->domain = inherit ? parent->domain : next->local->universe;
    for(index_t n = inherit ? parent->prefix_length : 0; n < axis; ++n)
        if(!restrict_local_axis(next->domain, *next->local, n, seed[n])) return false;
    next->prefix_length = axis;
    cursor = std::move(next);
    return true;
}

HC_info::completion_query HC_info::restrict_completion(
    completion_space &space, HC domain, index_t axis, const completion_query *parent) const
{
    auto candidates = space.cells.restrict(domain, parent ? &parent->candidates : nullptr);
    domain[axis] &= space.cells.supported(candidates, domain, axis);
    const auto mixed_kinds = [&](index_t n) {
        return int(domain[n].contains(null_coordinate))
            + int(domain[n].intersects(nonjump_entries[n])
                  && (domain[n].size() > 1 || !domain[n].contains(null_coordinate)))
            + int(domain[n].intersects(departing_entries[n]))
            + int(domain[n].intersects(arriving_entries[n])) > 1;
    };
    bool partition_kinds = false;
    if(axis < new_axis && axis + 2 >= new_axis)
        for(index_t n = axis; n < new_axis; ++n) partition_kinds |= mixed_kinds(n);
    if(partition_kinds)
    {
        // A small move-kind partition fixes branch counts and exposes unary
        // negatives near the end of the existing axes. Close both constraint
        // families together before testing individual coordinate choices.
        HC supported{std::vector<integer_set>(dimension())};
        const auto close = [&](const HC &bounds, std::span<const slice *const> negative, const HC &part) {
            bool closed = true;
            for(index_t n = 0; n < dimension(); ++n)
                if(!part[n].is_subset_of(bounds[n])) { closed = false; break; }
            HC effective = closed ? part : bounds;
            if(!closed)
                for(index_t n = 0; n < dimension(); ++n) effective[n] &= part[n];
            for(;;)
            {
                std::size_t before = 0, after_prune = 0, after_close = 0, after = 0;
                for(index_t n = 0; n < dimension(); ++n) before += effective[n].size();
                if(!prune_slice_refs(effective, negative)) return integer_set{};
                for(index_t n = 0; n < dimension(); ++n) after_prune += effective[n].size();
                // Each partition was already closed. If this cell contains it
                // and no negative narrowed it, closing the same domain again
                // cannot remove any additional coordinates.
                if((!closed || after_prune != before) && !constrain_domain(effective, &space.propagation)) return integer_set{};
                for(index_t n = 0; n < dimension(); ++n) after_close += effective[n].size();
                if(!prune_slice_refs(effective, negative)) return integer_set{};
                for(index_t n = 0; n < dimension(); ++n) after += effective[n].size();
                if(before == after)
                {
                    for(index_t n = 0; n < dimension(); ++n) supported[n] |= effective[n];
                    return effective[axis];
                }
                closed = after == after_close;
            }
        };
        const auto partition = [&](const auto &self, const HC &input, index_t n) -> void {
            for(int kind = 0; kind < 4; ++kind)
            {
                HC part = input;
                if(kind == 0) part[n] &= integer_set{null_coordinate};
                else if(kind == 1) { part[n] &= nonjump_entries[n]; part[n].erase(null_coordinate); }
                else part[n] &= kind == 2 ? departing_entries[n] : arriving_entries[n];
                if(part.empty() || !constrain_domain(part, &space.propagation)) continue;
                if(n + 1 < new_axis) { self(self, part, n + 1); continue; }
                space.cells.project(candidates, part, axis, [&](const HC &bounds, auto negative) {
                    return close(bounds, negative, part);
                }, true);
            }
        };
        partition(partition, domain, axis);
        for(index_t n = 0; n < dimension(); ++n) domain[n] &= supported[n];
    }
    if(!domain.empty() && !constrain_domain(domain, &space.propagation)) domain[axis].clear();
    if(!domain.empty()) candidates = space.cells.restrict(domain, &candidates);
    completion_query result(std::move(domain), std::move(candidates));
    result.prefix_length = axis;
    if(parent)
    {
        result.local = parent->local;
    }
    return result;
}

std::optional<slice> HC_info::project_local_problem(
    const local_space &local, const point &seed, const slice &problem) const
{
    slice projected;
    for(const auto &[axis, values] : problem.get_fixed_axes())
    {
        const auto found = std::lower_bound(local.axes.begin(), local.axes.end(), axis);
        index_t variable = no_axis;
        integer_set allowed;
        if(found != local.axes.end() && *found == axis)
        {
            variable = found - local.axes.begin();
            if(local.universe[variable].is_subset_of(values)) continue;
            allowed = local.universe[variable] & values;
        }
        else
        {
            for(index_t n = 0; n < local.axes.size(); ++n)
                if(const auto *arrival = std::get_if<arriving_entry>(&axis_coords[local.axes[n]][local.seed[n]]);
                   arrival && line_to_axis_unsafe(arrival->m.from.l()) == axis)
                {
                    variable = n;
                    for(auto coordinate : local.universe[n])
                        if(values.contains(std::get<arriving_entry>(axis_coords[local.axes[n]][coordinate]).idx))
                            allowed.insert(coordinate);
                    break;
                }
            if(variable == no_axis)
            {
                if(!values.contains(seed[axis])) return std::nullopt;
                continue;
            }
            if(local.universe[variable].is_subset_of(allowed)) continue;
        }
        if(const auto previous = projected.get_fixed_axes().find(variable);
           previous != projected.get_fixed_axes().end()) allowed &= previous->second;
        if(allowed.empty()) return std::nullopt;
        projected.fix_axis(variable, std::move(allowed));
    }
    return projected;
}
