#include "check_position.h"
#include <limits>
#include "magic.h"
#include "move_geometry.h"
#include <algorithm>
#include <array>
#include <cassert>

check_position::check_position(const state &s)
    : check_position(s, s.get_lines_range().first, s.get_lines_range().second) {}

check_position::check_position(const state &s, int l_min, int l_max)
    : base(s), first_line(l_min), lines(l_max - l_min + 1)
{
    const auto [first, last] = s.get_lines_range();
    assert(l_min <= first && last <= l_max);
    for (int l = first; l <= last; ++l) {
        auto &line = lines[l - l_min];
        line.exists = line.in_base = true;
        line.start = s.get_timeline_start(l);
        line.end = s.get_timeline_end(l);
    }
}

check_position check_position::for_present(const state &s, turn present)
{
    check_position result(s);
    const auto [lo, hi] = s.get_active_range();
    for (int l = lo; l <= hi; ++l) {
        const auto [t, c] = s.get_timeline_end(l).get();
        if (turn{t,c} == present) {
            result.add_board(l, turn{t,c}.next_turn(), *s.get_board_ptr(l,t,c));
        }
    }
    return result;
}

check_position check_position::for_phantom(const state &s)
{
    return for_present(s, s.get_present());
}

check_position check_position::for_check_symbols(const state &s)
{
    auto present = s.apparent_present();
    if (present.c() == s.get_present().c()) present = present.next_turn();
    return for_present(s, present);
}

check_position check_position::for_move_candidates(const state &s)
{
    const auto [lo, hi] = s.get_lines_range();
    const int branch = s.new_line();
    check_position result(s, std::min(lo,branch), std::max(hi,branch));
    const bool player = s.get_present().c();
    for (int l = lo; l <= hi; ++l) {
        const auto at = s.get_timeline_end(l);
        if (at.c() != player)
            result.add_board(l, at.next_turn(), *s.get_board_ptr(l,at.t(),at.c()));
    }
    return result;
}

template<check_position::check_kind Kind>
bool check_position::gives_check(board_overlay move_overlay, bool attacker,
                                 std::optional<board_overlay> departure_overlay)
{
    static_assert(Kind == check_kind::physical || Kind == check_kind::superphysical
                  || Kind == check_kind::historical);
    const std::array<board_overlay,2> boards{
        move_overlay, departure_overlay.value_or(move_overlay)
    };
    const std::size_t count = departure_overlay ? 2 : 1;
    std::array<int,2> sources{};
    std::array<line_view,2> saved{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto &update = boards[i];
        const turn start{update.t, update.c};
        assert(update.value);
        assert(update.l >= first_line && update.l-first_line < static_cast<int>(lines.size()));
        assert(i == 0 || update.l != sources[0]);
        sources[i] = update.l;
        auto &line = lines[update.l-first_line];
        saved[i] = line;
        if (!line.exists) line.start = start;
        line.exists = true;
        line.added_start = start;
        line.end = start.next_turn();
        line.added = update.value;
    }
    struct restore {
        check_position &view;
        std::span<const int> sources;
        const std::array<line_view,2> &saved;
        ~restore() {
            for (std::size_t i = 0; i < sources.size(); ++i)
                view.lines[sources[i]-view.first_line] = saved[i];
        }
    } guard{*this, std::span(sources).first(count), saved};
    auto emit = [&](full_move move) {
        if constexpr (Kind == check_kind::physical) {
            return move.from.tl() == move.to.tl();
        } else if constexpr (Kind == check_kind::superphysical) {
            return move.from.tl() != move.to.tl();
        } else {
            const auto &target = lines[move.to.l()-first_line];
            return turn{move.to.t(),attacker} != target.end;
        }
    };
    constexpr bool include_physical = Kind == check_kind::physical;
    constexpr bool include_superphysical = Kind != check_kind::physical;
    return attacker ? scan<true>(include_physical,emit,guard.sources,
                                  include_superphysical)
                    : scan<false>(include_physical,emit,guard.sources,
                                   include_superphysical);
}

template bool check_position::gives_check<check_position::check_kind::physical>(
    check_position::board_overlay, bool, std::optional<check_position::board_overlay>);
template bool check_position::gives_check<check_position::check_kind::superphysical>(
    check_position::board_overlay, bool, std::optional<check_position::board_overlay>);
template bool check_position::gives_check<check_position::check_kind::historical>(
    check_position::board_overlay, bool, std::optional<check_position::board_overlay>);

void check_position::add_board(int l, turn at, const board &b)
{
    assert(l >= first_line && l - first_line < static_cast<int>(lines.size()));
    auto &line = lines[l - first_line];
    assert(!line.added);
    if (line.exists) {
        assert(at == line.end.next_turn());
    } else {
        line.exists = true;
        line.start = at;
    }
    line.added_start = at;
    line.end = at;
    line.added = &b;
}

const board *check_position::get_board_ptr(int l, int t, bool c) const
{
    if (t < 0 || t > std::numeric_limits<int>::max() / 2
        || l < first_line || l - first_line >= static_cast<int>(lines.size()))
        return nullptr;
    const auto &line = lines[l - first_line];
    const turn at{t,c};
    if (!line.exists || at < line.start || at > line.end)
        return nullptr;
    if (line.added && at >= line.added_start)
        return line.added;
    return line.in_base ? base.get_board_ptr(l,t,c) : nullptr;
}

piece_t check_position::get_piece(vec4 p, bool c) const
{
    const board *b = get_board_ptr(p.l(), p.t(), c);
    assert(b);
    return b->get_piece(p.xy());
}

template<bool C, check_emitter Emit>
bool check_position::scan_physical(vec4 p0, const board &source,
                                   bitboard_t friendly, Emit &emit) const
{
    // Physical captures of enemy royals, including pawn and brawn diagonals.
    // En passant can uncover a check; scan the resulting board for that attack.
    for (int dst : marked_pos(source.royal() & source.hostile<C>())) {
        for (int src : marked_pos(source.attacks_to(dst) & friendly)) {
            if (emit(full_move(vec4(src,p0),vec4(dst,p0)))) return true;
        }
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan_pure_sliders(vec4 p0, const board &source,
                                       bitboard_t friendly, Emit &emit) const
{
    auto scan_direction = [&](vec4 direction, bitboard_t pieces) {
        for (vec4 q = p0+direction; pieces; q = q+direction) {
            const board *b = get_board_ptr(q.l(),q.t(),C);
            if (!b) break;
            const bitboard_t hits = pieces & b->royal() & b->hostile<C>();
            for (int xy : marked_pos(hits)) {
                if (emit(full_move(vec4(xy,p0),vec4(xy,q)))) return true;
            }
            // A capture or friendly blocker ends this ray for its source square.
            pieces &= ~b->occupied();
        }
        return false;
    };
    // Rook-like captures along time or timeline axes, without spatial movement.
    for (vec4 direction : orthogonal_tl_directions) {
        if (scan_direction(direction,source.lrook() & friendly)) return true;
    }
    // Bishop-like captures along time/timeline diagonals, at the same square.
    for (vec4 direction : diagonal_tl_directions) {
        if (scan_direction(direction,source.lbishop() & friendly)) return true;
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan_compound_sliders(vec4 p0, const board &source,
                                           bitboard_t friendly, Emit &emit) const
{
    auto scan_direction = [&](vec4 direction, bitboard_t orthogonal_pieces,
                              bitboard_t diagonal_pieces) {
        if (!(orthogonal_pieces | diagonal_pieces)) return false;
        std::array<bitboard_t,8> occupied_boards{}, royal_boards{};
        int count = 0;
        bitboard_t any_royal = 0;
        // Collect blockers and enemy royals on each board crossed by the ray.
        for (vec4 q = p0+direction; count < 7; q = q+direction) {
            const board *b = get_board_ptr(q.l(),q.t(),C);
            if (!b) break;
            ++count;
            occupied_boards[count] = b->occupied();
            royal_boards[count] = b->royal() & b->hostile<C>();
            any_royal |= royal_boards[count];
        }
        if (!any_royal) return false;
        // Project the spatial ray across those boards to find unobstructed captures.
        auto project = [&]<bool Diagonal>(bitboard_t pieces) {
            constexpr auto mask_fn = Diagonal ? bishop_copy_mask : rook_copy_mask;
            for (int src : marked_pos(pieces)) {
                bitboard_t occupied = 0, royals = 0;
                for (int n = 1; n <= count; ++n) {
                    const bitboard_t mask = mask_fn(src,n);
                    occupied |= mask & occupied_boards[n];
                    royals |= mask & royal_boards[n];
                }
                if (count < 7) occupied |= mask_fn(src,count+1);
                if (!royals) continue;
                const bitboard_t hits = royals & (Diagonal
                    ? bishop_attack(src,occupied) : rook_attack(src,occupied));
                for (int dst : marked_pos(hits)) {
                    const vec4 p(src,p0), xy(dst,p0);
                    const int distance = std::max(
                        std::abs(xy.x()-p.x()),std::abs(xy.y()-p.y()));
                    if (emit(full_move(p,vec4(dst,p0+direction*distance)))) return true;
                }
            }
            return false;
        };
        return project.template operator()<false>(orthogonal_pieces)
            || project.template operator()<true>(diagonal_pieces);
    };
    // One non-spatial axis plus one spatial axis (bishop) or two (unicorn).
    for (vec4 direction : orthogonal_tl_directions) {
        if (scan_direction(direction,source.lbishop() & friendly,
                           source.lunicorn() & friendly)) return true;
    }
    // Two non-spatial axes plus one spatial axis (unicorn) or two (dragon).
    for (vec4 direction : diagonal_tl_directions) {
        if (scan_direction(direction,source.lunicorn() & friendly,
                           source.ldragon() & friendly)) return true;
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan_jumps(vec4 p0, const board &source,
                                bitboard_t friendly, Emit &emit) const
{
    auto report = [&](vec4 p, vec4 q0, bitboard_t targets) {
        for (int dst : marked_pos(targets)) {
            if (emit(full_move(p,vec4(dst,q0)))) return true;
        }
        return false;
    };
    auto jump = [&]<class Attack>(vec4 direction, bitboard_t pieces, Attack attack)
        requires requires(Attack &function, int src) {
            { function(src) } -> std::same_as<bitboard_t>;
        }
    {
        if (!pieces) return false;
        const vec4 q = p0+direction;
        const board *b = get_board_ptr(q.l(),q.t(),C);
        if (!b) return false;
        const bitboard_t royals = b->royal() & b->hostile<C>();
        if (!royals) return false;
        for (int src : marked_pos(pieces)) {
            if (report(vec4(src,p0),q,attack(src) & royals)) return true;
        }
        return false;
    };
    const auto same_xy = [](int src) { return pmask(src); };
    // King and knight captures combining spatial movement with time or timeline.
    for (vec4 direction : orthogonal_tl_directions) {
        if (jump(direction,source.lking() & friendly,king_jump_attack)) return true;
        if (jump(direction,source.lknight() & friendly,knight_jump1_attack)) return true;
        if (jump(direction*2,source.lknight() & friendly,knight_jump2_attack)) return true;
    }
    // King captures along time/timeline diagonals, with optional spatial steps.
    for (vec4 direction : diagonal_tl_directions) {
        if (jump(direction,source.lking() & friendly,king_jump_attack)) return true;
    }
    // Knight captures using only time and timeline movement.
    for (vec4 direction : purely_superphysical_knight_directions) {
        if (jump(direction,source.lknight() & friendly,same_xy)) return true;
    }
    // Pawn and brawn captures: forward timeline plus past or future time.
    constexpr int forward_l = C ? 1 : -1;
    for (int dt : {-1,1}) {
        if (jump(vec4(0,0,dt,forward_l),source.lpawn() & friendly,same_xy)) return true;
    }
    const auto forward_rank = [](int src) {
        if constexpr (C) return shift_south(pmask(src));
        else return shift_north(pmask(src));
    };
    // Additional brawn captures: forward timeline plus forward rank or either file.
    if (jump(vec4(0,0,0,forward_l),source.lrawn() & friendly,[&](int src) {
        return forward_rank(src) | shift_west(pmask(src)) | shift_east(pmask(src));
    })) return true;
    // Additional brawn capture: forward rank plus past time on the same timeline.
    if (jump(vec4(0,0,-1,0),source.lrawn() & friendly,forward_rank)) return true;
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan(bool include_physical, Emit &&emit,
                           std::span<const int> sources,
                           bool include_superphysical) const
{
    auto scan_line = [&](int i) {
        const auto &line = lines[i];
        if (!line.exists || line.end.c() != C) return false;
        const vec4 p0(0,0,line.end.t(),first_line+i);
        const board &source = *get_board_ptr(p0.l(),p0.t(),C);
        const bitboard_t friendly = source.hostile<!C>(); // excludes walls
        if ((include_physical && scan_physical<C>(p0,source,friendly,emit))
            || (include_superphysical
                && (scan_pure_sliders<C>(p0,source,friendly,emit)
                    || scan_compound_sliders<C>(p0,source,friendly,emit)
                    || scan_jumps<C>(p0,source,friendly,emit)))) {
            return true;
        }
        return false;
    };
    if (!sources.empty()) {
        for (int l : sources) if (scan_line(l-first_line)) return true;
    } else {
        for (int i = 0; i < static_cast<int>(lines.size()); ++i)
            if (scan_line(i)) return true;
    }
    return false;
}

std::optional<full_move> check_position::first_check(
    bool attacker,
    bool include_physical
) const
{
    std::optional<full_move> result;
    auto emit = [&](full_move move) { result = move; return true; };
    if (attacker) scan<true>(include_physical,emit);
    else scan<false>(include_physical,emit);
    return result;
}

std::optional<full_move> check_position::first_check(
    bool attacker, board_overlay result,
    std::optional<board_overlay> departure, bool include_physical)
{
    const std::array<board_overlay, 2> updates{result, departure.value_or(result)};
    const std::size_t count = departure ? 2 : 1;
    std::array<line_view, 2> saved;
    for(std::size_t i = 0; i < count; ++i)
    {
        const auto &update = updates[i];
        assert(update.value);
        assert(update.l >= first_line && update.l - first_line < static_cast<int>(lines.size()));
        assert(i == 0 || update.l != updates[0].l);
        saved[i] = lines[update.l - first_line];
        add_board(update.l, turn{update.t, update.c}, *update.value);
    }
    struct restore
    {
        check_position &view;
        std::span<const board_overlay> updates;
        const std::array<line_view, 2> &saved;
        ~restore()
        {
            for(std::size_t i = 0; i < updates.size(); ++i)
                view.lines[updates[i].l - view.first_line] = saved[i];
        }
    } guard{*this, std::span(updates).first(count), saved};
    return std::as_const(*this).first_check(attacker, include_physical);
}

std::vector<full_move> check_position::checks(
    bool attacker,
    bool include_physical
) const
{
    std::vector<full_move> result;
    auto emit = [&](full_move move) { result.push_back(move); return false; };
    if (attacker) scan<true>(include_physical,emit);
    else scan<false>(include_physical,emit);
    return result;
}
