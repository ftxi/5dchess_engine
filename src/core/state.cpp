#include "state.h"
#include "check_position.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include "utils.h"
#include "pgnparser.h"
#include "pgn_loading.h"
#include "hypercuboid.h"

//#define DEBUGMSG
#include "debug.h"


state::state(multiverse mtv, promotion_options promotion_rules) noexcept
    : m(std::move(mtv)), present(m.get_present()), promotions(promotion_rules)
{}

template<bool UNSAFE>
state state::from_pgn(const std::string &pgn)
{
    const auto parsed = pgnparser(pgn).parse_game();
    if(!parsed) throw std::runtime_error("Bad input, parse failed");
    return from_ast<UNSAFE>(*parsed);
}

template<bool UNSAFE>
state state::from_ast(const pgnparser_ast::game &g)
{
    auto [initial, error] = pgn_loading::load_initial_position<UNSAFE>(g);
    if(error)
        throw *error;
    // parse moves
    const pgnparser_ast::gametree *gt = &g.gt;
    while(true)
    {
        if(!std::holds_alternative<pgnparser_ast::gametree::variations_t>(gt->variations_or_outcome))
        {
            // pgnparser_ast::token_t outcome = std::get<pgnparser_ast::token_t>(gt->variations_or_outcome);
            // (void)outcome;
            break;
        }

        const auto &variations = std::get<pgnparser_ast::gametree::variations_t>(gt->variations_or_outcome);
        if(variations.empty())
            break;

        const auto &[act, last_gt] = *(variations.end() - 1);
        //std::cout << act;
        for(const auto &mv: act.moves)
            initial.apply_move_ast(mv, g.source_text);
        if(!initial.test_present())
            throw pgn_validation_error(pgn_validation_error::present_not_advanced{},
                pgn_validation_error::make_subject(act, g.source_text));
        initial.template submit<true>();
        if constexpr (!UNSAFE) {
            if(auto exposure = initial.find_royal_exposure(false))
                throw pgn_validation_error(*exposure,
                    pgn_validation_error::make_subject(act, g.source_text));
        }
        gt = last_gt.get();
    }
    return initial;
}

template state state::from_pgn<false>(const std::string &pgn);
template state state::from_pgn<true>(const std::string &pgn);
template state state::from_ast<false>(const pgnparser_ast::game &g);
template state state::from_ast<true>(const pgnparser_ast::game &g);

int state::new_line() const noexcept
{
    auto [l_min, l_max] = m.get_lines_range();
    if(present.c() == 0)
        return l_max + 1;
    else
        return l_min - 1;
}

bool state::test_present() const
{
    return m.get_present().c() != present.c();
}

std::optional<state> state::can_submit() const
{
    state new_state = *this;
    bool flag = new_state.submit<false>();
    if(flag)
    {
        return std::optional<state>(new_state);
    }
    else
    {
        return std::nullopt;
    }
}

std::optional<state> state::can_apply(full_move fm) const
{
    state new_state = *this;
    if(!new_state.apply_move<false>(fm)) return std::nullopt;
    return new_state;
}

std::optional<state> state::can_apply(ext_move mv) const
{
    state new_state = *this;
    if(!new_state.apply_move<false>(mv)) return std::nullopt;
    return new_state;
}

std::optional<state> state::can_apply(const action &act) const
{
    state new_state = *this;
    for(const auto &em : act.get_moves_view())
    {
        bool flag = new_state.apply_move<false>(em);
        if(!flag)
        {
            return std::nullopt;
        }
    }
    bool flag = new_state.submit<false>();
    if(!flag)
    {
        return std::nullopt;
    }
    return std::make_optional<state>(new_state);
}

template<bool UNSAFE>
bool state::apply_move(full_move fm)
{
    const auto normalized = normalize_promotion<UNSAFE>(fm);
    if(!normalized) return false;
    return apply_move<UNSAFE>(*normalized);
}

template<bool UNSAFE>
bool state::apply_move(ext_move mv)
{
    const auto [fm, promote_to] = mv;
    dprint("applying move", fm);
    vec4 p = fm.from;
    vec4 q = fm.to;
    vec4 d = q - p;
    if constexpr (!UNSAFE)
    {
        const auto normalized = normalize_promotion(fm, promote_to);
        if(!normalized || *normalized != mv)
        {
            return false;
        }
        auto te = m.get_timeline_end(p.l());
        if(turn{p.t(), present.c()} != te)
            return false;
        const piece_t piece = get_piece(p, present.c());
        if(piece == NO_PIECE || piece_color(piece) != present.c())
            return false;
        auto mvs = present.c() ? m.gen_moves<true>(p) : m.gen_moves<false>(p);
        //auto it = mvbbs.find(q.tl());
        const auto &res = mvs.find([&q](const auto &pair){
            const auto &[tl, bb] = pair;
            return tl == q.tl();
        });
        // is it a pseudolegal move?
        if(res)
        {
            bitboard_t bb = res.value().second;
            if(!(pmask(q.xy()) & bb))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    
    /* WARNING: similiar logic used in hypercuboid.cpp for applying semimoves
     If some move logic needs to be changed here, make sure also perform change
     in HC_info::build_HC()
     */
    // physical move, no time travel
    if(d.tl() == vec4(0,0,0,0))
    {
        const board *b_ptr = m.get_board_ptr(p.l(), p.t(), present.c());
        bitboard_t z = pmask(p.xy());
        const auto &[size_x, size_y] = m.get_board_size();
        // en passant
        if((b_ptr->lpawn()&z) && d.x()!=0 && b_ptr->get_piece(q.xy()) == NO_PIECE)
        {
            dprint(" ... en passant");
            m.append_board(p.l(), b_ptr
                            ->replace_piece(ppos(q.x(),p.y()), NO_PIECE)
                            ->move_piece(p.xy(), q.xy()));
        }
        // promotion
        else if((b_ptr->lpawn()&z) && (q.y() == (present.c() ? 0 : size_y - 1)))
        {
            if(promotions == promotion_options::NONE)
            {
                dprint(" ... promotion disabled");
                m.append_board(p.l(), b_ptr->move_piece(p.xy(), q.xy()));
            }
            else
            {
                dprint(" ... promotion");
                piece_t promoted = present.c() ? to_black(promote_to) : promote_to;
                m.append_board(p.l(), b_ptr
                                ->replace_piece(p.xy(), NO_PIECE)
                                ->replace_piece(q.xy(), promoted));
            }
        }
        // castling
        else if((b_ptr->king()&z) && abs(d.x()) > 1)
        {
            dprint(" ... castling");
            const bitboard_t side = d.x() < 0 ? z - 1 : ~(z - 1);
            const int rook_from = bb_get_pos(b_ptr->castling_rooks(p.xy(), present.c()) & side);
            int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1); //rook's new x coordinate
            m.append_board(p.l(),b_ptr
                            ->move_piece(rook_from, ppos(rook_x2,q.y()))
                            ->move_piece(p.xy(), q.xy()));
        }
        // normal move
        else
        {
            dprint(" ... normal move/capture");
            m.append_board(p.l(), b_ptr->move_piece(p.xy(), q.xy()));
        }
    }
    // non-branching superphysical move
    else if (turn{q.t(), present.c()} == m.get_timeline_end(q.l()))
    {
        const board *b_ptr = m.get_board_ptr(p.l(), p.t(), present.c());
        const piece_t &pic = static_cast<piece_t>(piece_name(b_ptr->get_piece(p.xy())));
        m.append_board(p.l(), b_ptr->replace_piece(p.xy(), NO_PIECE));
        
        bitboard_t z = pmask(p.xy());
        const auto &[size_x, size_y] = m.get_board_size();
        const board *c_ptr = m.get_board_ptr(q.l(), q.t(), present.c());
        
        // promotion (only brawns can do)
        if ((b_ptr->lrawn()&z) && (q.y() == (present.c() ? 0 : size_y - 1)))
        {
            if(promotions == promotion_options::NONE)
            {
                dprint(" ... nonbranching brawn promotion disabled");
                m.append_board(q.l(), c_ptr->replace_piece(q.xy(), pic));
            }
            else
            {
                dprint(" ... nonbranching brawn promotion");
                piece_t promoted = present.c() ? to_black(promote_to) : promote_to;
                m.append_board(q.l(), c_ptr->replace_piece(q.xy(), promoted));
            }
        }
        // normal non_branching move
        else
        {
            dprint(" ... nonbranching jump");
            m.append_board(q.l(), c_ptr->replace_piece(q.xy(), pic));
        }
    }
    //branching move
    else
    {
        const board *b_ptr = m.get_board_ptr(p.l(), p.t(), present.c());
        const piece_t &pic = static_cast<piece_t>(piece_name(b_ptr->get_piece(p.xy())));
        m.append_board(p.l(), b_ptr->replace_piece(p.xy(), NO_PIECE));
        const board *x_ptr = m.get_board_ptr(q.l(), q.t(), present.c());
        auto [t, c] = turn{q.t(), present.c()}.next_turn().get();
        
        bitboard_t z = pmask(p.xy());
        const auto &[size_x, size_y] = m.get_board_size();
        
        // promotion (only brawns can do)
        if ((b_ptr->lrawn()&z) && (q.y() == (present.c() ? 0 : size_y - 1)))
        {
            if(promotions == promotion_options::NONE)
            {
                dprint(" ... branching brawn promotion disabled");
                m.insert_board(new_line(), t, c, x_ptr->replace_piece(q.xy(), pic));
            }
            else
            {
                dprint(" ... branching brawn promotion");
                piece_t promoted = present.c() ? to_black(promote_to) : promote_to;
                m.insert_board(new_line(), t, c, x_ptr->replace_piece(q.xy(), promoted));
            }
        }
        // normal non_branching move
        else
        {
            dprint(" ... branching jump");
            m.insert_board(new_line(), t, c, x_ptr->replace_piece(q.xy(), pic));
        }
        const int new_present = m.get_present().t();
        if(new_present < present.t())
        {
            // if a historical board is activated by this travel, go back
            present = turn{new_present, present.c()};
        }
    }
    return true;
}

template<bool UNSAFE>
std::optional<ext_move> state::normalize_promotion(full_move move, piece_t requested) const
{
    const vec4 p = move.from;
    const vec4 q = move.to;
    const vec4 d = q - p;
    const auto &[size_x, size_y] = get_board_size();
    const board *source = nullptr;
    if constexpr (!UNSAFE)
    {
        const auto in_bounds = [size_x, size_y](vec4 square) {
            return square.x() >= 0 && square.x() < size_x
                && square.y() >= 0 && square.y() < size_y;
        };
        if(!in_bounds(p) || !in_bounds(q))
            return std::nullopt;
        source = get_board_ptr(p.l(), p.t(), present.c());
        if(!source)
            return std::nullopt;
    }
    const bool last_rank = q.y() == (present.c() ? 0 : size_y - 1);
    if(!last_rank)
    {
        return ext_move(move, NO_PIECE);
    }
    const piece_t piece = piece_name([&] {
        if constexpr (UNSAFE)
            return get_piece(p, present.c());
        else
            return source->get_piece(p.xy());
    }());
    const bool physical_promotion = d.l() == 0 && d.t() == 0
        && (piece == PAWN_W || piece == PAWN_B || piece == BRAWN_W || piece == BRAWN_B)
        && last_rank;
    const bool superphysical_promotion = (d.l() != 0 || d.t() != 0)
        && (piece == BRAWN_W || piece == BRAWN_B)
        && last_rank;
    if(!physical_promotion && !superphysical_promotion)
    {
        return ext_move(move, NO_PIECE);
    }

    if(promotions == promotion_options::NONE)
    {
        if(requested != NO_PIECE)
        {
            return std::nullopt;
        }
        return ext_move(move, requested);
    }
    if(requested == NO_PIECE)
    {
        requested = default_promote_to(promotions);
    }
    if(!can_promote_to(promotions, requested))
    {
        return std::nullopt;
    }
    return ext_move(move, requested);
}

template <bool UNSAFE>
bool state::submit()
{
    const turn next_present = m.get_present();
    if constexpr (!UNSAFE)
    {
        if(!test_present() || check_position(*this).first_check(next_present.c()))
        {
            return false;
        }
    }
    present = next_present;
    return true;
}

bool state::has_phantom_check() const
{
    return check_position::for_phantom(*this).first_check(!present.c()).has_value();
}

std::optional<pgn_validation_error::royal_exposed> state::find_royal_exposure(
    bool is_from_initial_setup
) const
{
    const check_position checks(*this);
    // After submit(), player is the opponent of the player who just moved.
    bool attacker = is_from_initial_setup ? false : present.c();
    auto capture = checks.first_check(attacker);
    if(is_from_initial_setup && !capture) {
        attacker = true;
        capture = checks.first_check(attacker);
    }
    if(!capture) return std::nullopt;

    // The attacker may differ from the stored player, so the ordinary move
    // printer's player-dependent board lookup cannot be used here.
    const auto from = capture->from, to = capture->to;
    std::ostringstream notation;
    notation << pretty_lt(from.tl())
        << static_cast<char>(to_white(piece_name(get_piece(from, attacker))))
        << static_cast<char>('a' + from.x()) << static_cast<char>('1' + from.y());
    if(from.tl() != to.tl()) {
        const bool branching = turn{to.t(), attacker} < get_timeline_end(to.l());
        notation << (branching ? ">>x" : ">x") << pretty_lt(to.tl());
    }
    else notation << 'x';
    notation << static_cast<char>('a' + to.x()) << static_cast<char>('1' + to.y());
    return pgn_validation_error::royal_exposed{
        .is_from_initial_setup = is_from_initial_setup,
        .capture = *capture,
        .notation = notation.str(),
    };
}



state state::phantom() const
{
    const auto [l_min, l_max] = m.get_active_range();
    state s = *this;
    for(int l = l_min; l <= l_max; l++)
    {
        auto turn = get_timeline_end(l);
        if(turn == get_present())
        {
            auto [t, c] = turn.get();
            s.m.append_board(l, m.get_board(l, t, c));
        }
    }
    return s;
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> state::get_timeline_status() const
{
    return get_timeline_status(present.t(), present.c());
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> state::get_timeline_status(int present_t, bool present_c) const
{
    auto [l_min, l_max] = m.get_lines_range();
    auto [active_min, active_max] = m.get_active_range();
    turn present_tc{present_t, present_c};
    std::vector<int> mandatory_timelines, optional_timelines, unplayable_timelines;
    for(int l = l_min; l <= l_max; l++)
    {
        turn tc = m.get_timeline_end(l);
        if (active_min <= l && active_max >= l && tc == present_tc)
        {
            mandatory_timelines.push_back(l);
        }
        else
        {
            auto [t, c] = tc.get();
            if(present_c==c)
            {
                optional_timelines.push_back(l);
            }
            else
            {
                unplayable_timelines.push_back(l);
            }
        }
    }
    return std::make_tuple(mandatory_timelines, optional_timelines, unplayable_timelines);
}


/**********
 *********
 ********
 *******
 ******
 *****
 ****
 ***
 **
 */

generator<full_move> state::find_checks(bool attacker) const &
{
    auto [l_min, l_max] = m.get_lines_range();
    std::vector<int> lines;
    for(int i = l_min; i <= l_max; i++)
    {
        const bool color = m.get_timeline_end(i).c();
        if(color == attacker)
        {
            lines.push_back(i);
        }
    }
    if (attacker)
    {
        return find_checks_impl<true>(lines);
    }
    else
    {
        return find_checks_impl<false>(lines);
    }
}

template<bool C>
generator<full_move> state::find_checks_impl(std::vector<int> lines) const &
{
//    print_range(__PRETTY_FUNCTION__, lines);
    for (int l : lines)
    {
        // take the active board
        auto [t, c] = m.get_timeline_end(l).get();
        assert(c == C);
        const board *b_ptr = m.get_board_ptr(l, t, C);
        bitboard_t b_pieces = b_ptr->friendly<C>() & ~b_ptr->wall();
        // for each friendly piece on this board
        for (int src_pos : marked_pos(b_pieces))
        {
            vec4 p = vec4(src_pos, vec4(0,0,t,l));
            // generate the aviliable moves
            auto moves = m.gen_moves<C>(p);
            // for each destination board and bit location
            for (const auto &[q0, bb] : moves)
            {
                const board *b1_ptr = m.get_board_ptr(q0.l(), q0.t(), C);
                if (bb)
                {
                    // if the destination square is royal, this is a check
                    bitboard_t c_pieces = bb & b1_ptr->royal();
                    if (c_pieces)
                    {
                        for(int dst_pos : marked_pos(c_pieces))
                        {
                            vec4 q = vec4(dst_pos, q0);
                            dprint("found check", full_move(p,q), "source:", p);
                            co_yield full_move(p, q);
                        }
                    }
                }
            }
        }
    }
}


std::vector<vec4> state::get_movable_pieces() const
{
    auto [mandatory_timelines, optional_timelines, unplayable_timelines] = get_timeline_status(present.t(), present.c());
    auto lines = concat_vectors(mandatory_timelines, optional_timelines);
    return get_movable_pieces(lines);
}

std::vector<vec4> state::get_movable_pieces(const std::vector<int> &lines) const
{
    if (present.c() == 0)
    {
        return gen_movable_pieces_impl<false>(lines);
    }
    else
    {
        return gen_movable_pieces_impl<true>(lines);
    }
}

std::vector<vec4> state::get_all_pieces(const std::vector<int> &lines) const
{
    std::vector<vec4> result;
    for(int l : lines)
    {
        const int t = get_timeline_end(l).t();
        const vec4 p0(0, 0, t, l);
        const board *b = m.get_board_ptr(l, t, present.c());
        const bitboard_t pieces = (present.c() ? b->black() : b->white()) & ~b->wall();
        for(int pos : marked_pos(pieces))
        {
            result.emplace_back(pos, p0);
        }
    }
    return result;
}

template <bool C>
std::vector<vec4> state::gen_movable_pieces_impl(const std::vector<int> &lines) const
{
    dprint("gen_movable_pieces_impl()");
    std::vector<vec4> result;
    for (int l : lines)
    {
        // take the active board
        const int t = get_timeline_end(l).t();
        const vec4 p0 = vec4(0,0,t,l);
        const board *b_ptr = m.get_board_ptr(l, t, C);
        bitboard_t b_pieces = b_ptr->friendly<C>() & ~b_ptr->wall();
        // for each friendly piece on this board
        for (int src_pos : marked_pos(b_pieces))
        {
            vec4 p = vec4(src_pos, p0);
            // generate the aviliable moves
            auto moves = m.gen_moves<C>(p);
            // for each destination board and bit location
            if(auto info = moves.first())
            {
                result.push_back(p);
            }
        }
    }
    dprint(range_to_string(result));
    return result;
}

mate_type state::get_mate_type(bool legal_action_witness) const
{
    dprint("state::get_mate_type()");
    auto [w, ss] = HC_info::build_HC(*this);
    search_space ss1;
    ss1.push_back(std::move(ss.back()));
    ss.pop_back();
    // check if there is a non-branching move
    if(w.search(std::move(ss1)).first())
    {
        dprint("has non-branching action");
        return mate_type::NONE;
    }
    const index_t max_branches = w.dimension() - w.first_branch_axis();
    index_t branch_cap = max_branches;
    index_t max_reactivate = 0;
    const auto [active_min, active_max] = get_active_range();
    const auto [l_min, l_max] = get_lines_range();
    const int direction = present.c() ? 1 : -1;
    const int first_inactive = present.c() ? active_max + 1 : active_min - 1;
    // Each branch activates the next opponent timeline, nearest first. An
    // earlier endpoint cannot catch up to the original Present in this action.
    for(int l = first_inactive; l_min <= l && l <= l_max; l += direction)
    {
        if(get_timeline_end(l) < present)
        {
            branch_cap = std::min(max_branches, max_reactivate);
            break;
        }
        ++max_reactivate;
    }

    const auto [l0_min, l0_max] = get_initial_lines_range();
    const int whites_lines = l_max - l0_max;
    const int blacks_lines = l0_min - l_min;
    const int timeline_advantage = present.c() ? (whites_lines - blacks_lines) : (blacks_lines - whites_lines);
    const index_t active_branch_cap = static_cast<index_t>(std::max(0, timeline_advantage + 1));
    search_space ss2;
    // build_HC orders these cases from max_branches down to one branch.
    // Preserve ss for the unrestricted legal-action search below.
    index_t branches = max_branches; // Number of new timelines required by the current hypercuboid.
    for(const HC &source : ss)
    {
        const index_t count = branches;
        --branches;
        if(count > branch_cap)
        {
            continue;
        }
        ss2.push_back(source);
        HC &hc = ss2.back();
        const index_t active_branches = std::min(count, active_branch_cap);
        const index_t end_axis = w.first_branch_axis() + active_branches;
        // Only active new timelines can rewind the Present through arrival.
        for(index_t n = w.first_branch_axis(); n < end_axis; n++)
        {
            hc[n].erase_if([&w, n, old_t=present.t()](int i){
                semimove sm = w.get_semimove(n, i);
                if(sm.is<arriving_move>())
                {
                    const auto &am = sm.get<arriving_move>();
                    int new_t = am.m.to.t();
                    return new_t < old_t;
                }
                return false;
            });
        }
    }
    if(w.search(std::move(ss2)).first())
    {
        dprint("has branching non-jump back solution");
        return mate_type::NONE;
    }
    if(legal_action_witness)
    {
        if(has_phantom_check())
        {
            dprint("softmate (legal action witnessed)");
            return mate_type::SOFTMATE;
        }
        dprint("not softmate (legal action witnessed without check)");
        return mate_type::NONE;
    }
    if(w.search(std::move(ss)).first())
    {
        if(has_phantom_check())
        {
            dprint("softmate");
            return mate_type::SOFTMATE;
        }
        else
        {
            dprint("almost softmate except for not checking opponent");
            return mate_type::NONE;
        }
    }
    else
    {
        if(has_phantom_check())
        {
            dprint("checkmate");
            return mate_type::CHECKMATE;
        }
        else
        {
            dprint("stalemate");
            return mate_type::STALEMATE;
        }
    }
}

std::pair<int, int> state::get_board_size() const noexcept
{
    return m.get_board_size();
}

std::string state::pretty_l(int l) const
{
    return m.pretty_l(l);
}

std::string state::pretty_lt(vec4 p0) const
{
    return m.pretty_lt(p0);
}

turn state::get_present() const noexcept
{
    return present;
}

turn state::apparent_present() const noexcept
{
    return m.get_present();
}

std::pair<int, int> state::get_initial_lines_range() const noexcept
{
    return m.get_initial_lines_range();
}

std::pair<int, int> state::get_lines_range() const noexcept
{
    return m.get_lines_range();
}

std::pair<int, int> state::get_active_range() const noexcept
{
    return m.get_active_range();
}

turn state::get_timeline_start(int l) const
{
    return m.get_timeline_start(l);
}

turn state::get_timeline_end(int l) const
{
    return m.get_timeline_end(l);
}

piece_t state::get_piece(vec4 p, bool color) const
{
    return m.get_piece(p, color);
}

std::shared_ptr<const board> state::get_board(int l, int t, bool c) const
{
    return m.get_board(l, t, c);
}

std::vector<std::tuple<int, int, bool, std::string>> state::get_boards() const
{
    return m.get_boards();
}

generator<vec4> state::gen_piece_move(vec4 p) const &
{
    return m.gen_piece_move(p, present.c());
}

std::string state::to_string() const
{
    std::ostringstream ss;
    ss << "State(present=" << present.t() << ", player=" << present.c() << "):\n";
    return ss.str() + m.to_string();
}

std::string state::show_fen() const
{
    std::ostringstream oss;
    for(const auto &[l,t,c,s] : m.get_boards<true>())
    {
        oss << "[" << s << ":" << m.pretty_l(l);
        oss << ":" << t << ":" << (c?"b":"w") << "]\n";
    }
    return oss.str();
}

ext_move state::apply_move_ast(const pgnparser_ast::move &move,
    const std::shared_ptr<const std::string> &source_text)
{
    using error = pgn_validation_error;
    const auto subject = [&] { return error::make_subject(move, source_text); };
    auto [fm, promotion, candidates] = parse_move(move);
    if(!fm) {
        if(candidates.empty())
            throw error(error::invalid_move{}, subject());
        const auto requested = std::visit([](const auto &mv) {
            return mv.promote_to;
        }, move.data);
        const piece_t promote_to = requested ? static_cast<piece_t>(*requested)
            : default_promote_to(promotions);
        error::ambiguous_move reason;
        reason.candidates.reserve(candidates.size());
        for(const auto &candidate : candidates) {
            const ext_move interpreted(candidate, promote_to);
            reason.candidates.push_back({normalize_promotion(candidate, promote_to).value_or(interpreted),
                candidate.pgn(*this, promote_to)});
        }
        throw error(std::move(reason), subject());
    }
    const piece_t requested = promotion.value_or(NO_PIECE);
    const auto normalized = normalize_promotion(*fm, requested);
    if(!normalized)
        throw error(error::illegal_promotion{requested, promotions}, subject());
    if(!apply_move<false>(*normalized))
        throw error(error::illegal_move{*normalized}, subject());
    return *normalized;
}

state::parse_pgn_res state::parse_move(const pgnparser_ast::move &move) const
{
    std::vector<full_move> matched;
    std::vector<full_move> pawn_move_matched;
    std::optional<full_move> fm;
    std::optional<piece_t> promotion;
    dprint("parse_move(",move,")");
    constexpr static pgn_options OPTIONS = pgn_options::SHOW_PAWN | pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION;
    if(std::holds_alternative<pgnparser_ast::physical_move>(move.data))
    {
        auto mv = std::get<pgnparser_ast::physical_move>(move.data);
        // for all physical moves avilable in current state
        for(vec4 p : get_movable_pieces())
        {
            char piece = to_white(piece_name(get_piece(p, present.c())));
            bitboard_t bb = present.c() ? m.gen_physical_moves<true>(p) : m.gen_physical_moves<false>(p);
            for(int pos : marked_pos(bb))
            {
                vec4 q(pos, p.tl());
                full_move fm(p,q);
                dprint("matching", fm);
                // test if this physical move matches any of them
                const piece_t promote_to = mv.promote_to
                    ? static_cast<piece_t>(*mv.promote_to)
                    : default_promote_to(promotions);
                std::string full_notation = fm.pgn_impl(*this, promote_to, OPTIONS, 0, false);
                auto full = pgnparser(full_notation).parse_physical_move();
                assert(full.has_value());
                bool match = pgnparser::match_physical_move(mv, *full);
                if(match)
                {
                    dprint("matched");
                    matched.push_back(fm);
                    if(piece == PAWN_W)
                    {
                        pawn_move_matched.push_back(fm);
                    }
                }
            }
        }
        if(matched.size()==1)
        {
            // if there is exactly one match, we are good
            fm = matched[0];
        }
        else if(pawn_move_matched.size() == 1)
        {
            /* if there are more than one match, test if it this can be
             parsed as the unique pawn move
             */
            fm = pawn_move_matched[0];
        }
        if(fm.has_value())
        {
            promotion = mv.promote_to.transform([](char pt){
                return static_cast<piece_t>(pt);
            });
        }
    }
    else if(std::holds_alternative<pgnparser_ast::superphysical_move>(move.data))
    {
        // do the same for superphysical moves
        auto spm = std::get<pgnparser_ast::superphysical_move>(move.data);
        bool is_relative = std::holds_alternative<pgnparser_ast::relative_board>(spm.to_board);
        for(vec4 p : get_movable_pieces())
        {
            char piece = to_white(piece_name(get_piece(p, present.c())));
            auto gen = present.c() ? m.gen_superphysical_moves<true>(p) : m.gen_superphysical_moves<false>(p);
            for(const auto &[p0, bb] : gen)
            {
                for(int pos : marked_pos(bb))
                {
                    vec4 q(pos, p0);
                    full_move fm(p,q);
                    dprint("matching", fm);
                    // test if this physical move matches any of them
                    std::string full_notation;
                    if(is_relative)
                    {
                        const piece_t promote_to = spm.promote_to
                            ? static_cast<piece_t>(*spm.promote_to)
                            : default_promote_to(promotions);
                        full_notation = fm.pgn_impl(*this, promote_to,
                            OPTIONS | pgn_options::SHOW_RELATIVE, 0, false);
                    }
                    else
                    {
                        const piece_t promote_to = spm.promote_to
                            ? static_cast<piece_t>(*spm.promote_to)
                            : default_promote_to(promotions);
                        full_notation = fm.pgn_impl(*this, promote_to, OPTIONS, 0, false);
                    }
                    auto full = pgnparser(full_notation).parse_superphysical_move();
                    assert(full.has_value());
                    bool match = pgnparser::match_superphysical_move(spm, *full);
                    if(match)
                    {
                        dprint("matched");
                        matched.push_back(fm);
                        if(piece == PAWN_W)
                        {
                            pawn_move_matched.push_back(fm);
                        }
                    }
                }
            }
        }
        if(matched.size()==1)
        {
            fm = matched[0];
        }
        else if(pawn_move_matched.size() == 1)
        {
            fm = pawn_move_matched[0];
        }
        if(fm.has_value())
        {
            promotion = spm.promote_to.transform([](char pt){
                return static_cast<piece_t>(pt);
            });
        }
    }
    return std::make_tuple(fm, promotion, matched);
}

state::parse_pgn_res state::parse_move(const std::string &move) const
{
    auto parsed_move = pgnparser(move).parse_standalone_move();
    if(!parsed_move.has_value())
    {
        return std::make_tuple(std::nullopt, std::nullopt, std::vector<full_move>{});
    }
    return parse_move(*parsed_move);
}

template std::optional<ext_move> state::normalize_promotion<false>(full_move, piece_t) const;
template std::optional<ext_move> state::normalize_promotion<true>(full_move, piece_t) const;
template bool state::apply_move<false>(full_move);
template bool state::apply_move<true>(full_move);
template bool state::apply_move<false>(ext_move);
template bool state::apply_move<true>(ext_move);
template bool state::submit<false>();
template bool state::submit<true>();

template generator<full_move> state::find_checks_impl<false>(std::vector<int>) const &;
template generator<full_move> state::find_checks_impl<true>(std::vector<int>) const &;
template std::vector<vec4> state::gen_movable_pieces_impl<false>(const std::vector<int> &) const;
template std::vector<vec4> state::gen_movable_pieces_impl<true>(const std::vector<int> &) const;
