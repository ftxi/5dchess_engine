#include "check_position.h"
#include "magic.h"
#include "move_geometry.h"
#include <algorithm>
#include <cassert>

check_position::check_position(const state& s)
    : check_position(s, s.get_lines_range().first, s.get_lines_range().second) {}

check_position::check_position(const state& s, int l_min, int l_max)
    : base(s), first_line(l_min), lines(l_max - l_min + 1)
{
    const auto [first, last] = s.get_lines_range();
    assert(l_min <= first && last <= l_max);
    for (int l = first; l <= last; ++l) {
        auto& line = lines[l - l_min];
        line.exists = line.in_base = true;
        line.start = s.get_timeline_start(l);
        line.end = s.get_timeline_end(l);
    }
}

check_position check_position::for_phantom(const state& s)
{
    check_position result(s);
    const bool player = s.get_present().second;
    for (int i = 0; i < static_cast<int>(result.lines.size()); ++i) {
        const auto [t, c] = result.lines[i].end;
        if (c == player) {
            const int l = result.first_line + i;
            result.add_board(l, next_turn({t,c}), *s.get_board_ptr(l,t,c));
        }
    }
    return result;
}

void check_position::add_board(int l, turn_t at, const board& b)
{
    assert(l >= first_line && l - first_line < static_cast<int>(lines.size()));
    auto& line = lines[l - first_line];
    assert(!line.added);
    if (line.exists) {
        assert(at == next_turn(line.end));
    } else {
        line.exists = true;
        line.start = at;
    }
    line.end = at;
    line.added = &b;
}

const board* check_position::board_at(int l, int t, bool c) const
{
    if (l < first_line || l - first_line >= static_cast<int>(lines.size()))
        return nullptr;
    const auto& line = lines[l - first_line];
    const turn_t at{t,c};
    if (!line.exists || at < line.start || at > line.end)
        return nullptr;
    if (line.added && at == line.end)
        return line.added;
    return line.in_base ? base.get_board_ptr(l,t,c) : nullptr;
}

piece_t check_position::get_piece(vec4 p, bool c) const
{
    const board* b = board_at(p.l(), p.t(), c);
    assert(b);
    return b->get_piece(p.xy());
}

template<bool C, check_emitter Emit>
bool check_position::scan_physical(vec4 p0, const board& source,
                                   bitboard_t friendly, Emit& emit) const
{
    for (int dst : marked_pos(source.royal() & source.hostile<C>())) {
        for (int src : marked_pos(source.attacks_to(dst) & friendly)) {
            if (emit(full_move(vec4(src,p0),vec4(dst,p0)))) return true;
        }
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan_pure_sliders(vec4 p0, const board& source,
                                       bitboard_t friendly, Emit& emit) const
{
    auto scan_direction = [&](vec4 direction, bitboard_t pieces) {
        for (vec4 q = p0+direction; pieces; q = q+direction) {
            const board* b = board_at(q.l(),q.t(),C);
            if (!b) break;
            const bitboard_t hits = pieces & b->royal() & b->hostile<C>();
            for (int xy : marked_pos(hits)) {
                if (emit(full_move(vec4(xy,p0),vec4(xy,q)))) return true;
            }
            pieces &= ~b->occupied();
        }
        return false;
    };
    for (vec4 direction : orthogonal_tl_directions) {
        if (scan_direction(direction,source.lrook() & friendly)) return true;
    }
    for (vec4 direction : diagonal_tl_directions) {
        if (scan_direction(direction,source.lbishop() & friendly)) return true;
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan_compound_sliders(vec4 p0, const board& source,
                                           bitboard_t friendly, Emit& emit) const
{
    auto scan_direction = [&](vec4 direction, bitboard_t orthogonal_pieces,
                              bitboard_t diagonal_pieces) {
        if (!(orthogonal_pieces | diagonal_pieces)) return false;
        std::array<bitboard_t,8> occupied_boards{}, royal_boards{};
        int count = 0;
        bitboard_t any_royal = 0;
        for (vec4 q = p0+direction; count < 7; q = q+direction) {
            const board* b = board_at(q.l(),q.t(),C);
            if (!b) break;
            ++count;
            occupied_boards[count] = b->occupied();
            royal_boards[count] = b->royal() & b->hostile<C>();
            any_royal |= royal_boards[count];
        }
        if (!any_royal) return false;
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
    for (vec4 direction : orthogonal_tl_directions) {
        if (scan_direction(direction,source.lbishop() & friendly,
                           source.lunicorn() & friendly)) return true;
    }
    for (vec4 direction : diagonal_tl_directions) {
        if (scan_direction(direction,source.lunicorn() & friendly,
                           source.ldragon() & friendly)) return true;
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan_jumps(vec4 p0, const board& source,
                                bitboard_t friendly, Emit& emit) const
{
    auto report = [&](vec4 p, vec4 q0, bitboard_t targets) {
        for (int dst : marked_pos(targets)) {
            if (emit(full_move(p,vec4(dst,q0)))) return true;
        }
        return false;
    };
    auto jump = [&]<class Attack>(vec4 direction, bitboard_t pieces, Attack attack)
        requires requires(Attack& function, int src) {
            { function(src) } -> std::same_as<bitboard_t>;
        }
    {
        if (!pieces) return false;
        const vec4 q = p0+direction;
        const board* b = board_at(q.l(),q.t(),C);
        if (!b) return false;
        const bitboard_t royals = b->royal() & b->hostile<C>();
        if (!royals) return false;
        for (int src : marked_pos(pieces)) {
            if (report(vec4(src,p0),q,attack(src) & royals)) return true;
        }
        return false;
    };
    const auto same_xy = [](int src) { return pmask(src); };
    for (vec4 direction : orthogonal_tl_directions) {
        if (jump(direction,source.lking() & friendly,king_jump_attack)) return true;
        if (jump(direction,source.lknight() & friendly,knight_jump1_attack)) return true;
        if (jump(direction*2,source.lknight() & friendly,knight_jump2_attack)) return true;
    }
    for (vec4 direction : diagonal_tl_directions) {
        if (jump(direction,source.lking() & friendly,king_jump_attack)) return true;
    }
    for (vec4 direction : purely_superphysical_knight_directions) {
        if (jump(direction,source.lknight() & friendly,same_xy)) return true;
    }
    constexpr int forward_l = C ? 1 : -1;
    for (int dt : {-1,1}) {
        if (jump(vec4(0,0,dt,forward_l),source.lpawn() & friendly,same_xy)) return true;
    }
    // Match multiverse::gen_moves_impl: additional L-direction brawn
    // captures are enabled only in its white pawn/brawn branch.
    if constexpr (!C) {
        if (jump(vec4(0,0,0,forward_l),source.lrawn() & friendly,[](int src) {
            const bitboard_t source_mask = pmask(src);
            return shift_north(source_mask) |
                shift_west(source_mask) | shift_east(source_mask);
        })) return true;
    }
    return false;
}

template<bool C, check_emitter Emit>
bool check_position::scan(bool include_physical, Emit&& emit) const
{
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const auto& line = lines[i];
        if (!line.exists || line.end.second != C) continue;
        const vec4 p0(0,0,line.end.first,first_line+i);
        const board& source = *board_at(p0.l(),p0.t(),C);
        const bitboard_t friendly = source.hostile<!C>(); // excludes walls
        if ((include_physical && scan_physical<C>(p0,source,friendly,emit))
            || scan_pure_sliders<C>(p0,source,friendly,emit)
            || scan_compound_sliders<C>(p0,source,friendly,emit)
            || scan_jumps<C>(p0,source,friendly,emit)) {
            return true;
        }
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
