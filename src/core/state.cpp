#include "state.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include "utils.h"
#include "pgnparser.h"
#include "hypercuboid.h"

//#define DEBUGMSG
#include "debug.h"


state::state(multiverse &mtv) noexcept : m(mtv.clone())
{
    std::tie(present, player) = m->get_present();
}

state::state(const pgnparser_ast::game &g)
{
    auto &metadata = g.headers;
    // parse size
    auto find_or_default = [](const std::map<std::string, std::string>& m, const std::string& key, const std::string& def) -> std::string {
        auto it = m.find(key);
        if (it != m.end()) {
            return it->second;
        } else {
            return def;
        }
    };
    std::string size_str = find_or_default(metadata, "size", "8x8");
    int size_x, size_y;
    auto pos = size_str.find('x');
    if (pos == std::string::npos)
        throw std::runtime_error("state(): Invalid board size format: " + size_str);
    try {
        size_x = std::stoi(size_str.substr(0, pos));
        size_y = std::stoi(size_str.substr(pos + 1));
        if(size_x <= 0 || size_y <= 0 || size_x > BOARD_LENGTH || size_y > BOARD_LENGTH)
        {
            throw std::out_of_range("");
        }
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("state(): Expect number in size value: " + size_str);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("state(): Number out of range in size value: " + size_str + " (max board size allowed: " + std::to_string(BOARD_LENGTH) + ")");
    }
    // parse board
    using board_t = std::vector<std::tuple<std::string, pgnparser_ast::token_t, int, int, bool>>;
    board_t boards = g.boards;
    std::optional<bool> is_even_timelines;
    auto it = metadata.find("board");
    if(it != metadata.end())
    {
        std::string board_str = it->second;
        // none of the default variants should be named as "Custom[...]"
        const static std::map<std::string, std::tuple<bool, int, int, board_t>> default_variants = {
            {
                "Standard",
                {
                    false, // Odd timelines
                    8,8,
                    {
                        std::make_tuple("r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*", pgnparser_ast::NIL, 0, 1, false)
                    }
                }
            },
            {
                "Standard - Turn Zero",
                {
                    false, // Odd timelines
                    8,8,
                    {
                        std::make_tuple("r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*", pgnparser_ast::NIL, 0, 0, true),
                        std::make_tuple("r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*", pgnparser_ast::NIL, 0, 1, false),
                    }
                }
            },
            {
                "Very Small - Open",
                {
                    false, // Odd timelines
                    4,4,
                    {
                        std::make_tuple("nbrk/3p*/P*3/KRBN", pgnparser_ast::NIL, 0, 1, false),
                    }
                }
            },
        };
        
        // Custom variant, can specify even/odd timelines here
        if(board_str == "Custom - Even" || board_str == "Even")
        {
            is_even_timelines = true;
        }
        else if(board_str == "Custom - Odd" || board_str == "Odd")
        {
            is_even_timelines = false;
        }
        else if(board_str.starts_with("Custom"))
        {
            // do nothing
        }
        else if(boards.empty())
        {
            // if g.board is not empty, any description in "Board" header is ignored
            try {
                bool even;
                std::tie(even, size_x, size_y, boards) = default_variants.at(board_str);
                is_even_timelines = even;
            } catch (const std::out_of_range&) {
                throw std::runtime_error("state(): Unknown variant: " + board_str);
            }
        }
    }
    if(boards.empty())
    {
        throw std::runtime_error("state(): Variant is unspecific: no Board header or 5DFEN given");
    }
    if(!is_even_timelines.has_value())
    {
        bool even = false;
        for(const auto& [fen, sign, l, t, c] : boards)
        {
            even |= (sign == pgnparser_ast::POSITIVE && l == 0);
            even |= (sign == pgnparser_ast::NEGATIVE && l == 0);
            if(even) break;
        }
        is_even_timelines = even;
    }
    // construct multiverse
    std::vector<boards_info_t> boards_info(boards.size());
    if(*is_even_timelines)
    {
        std::transform(boards.begin(), boards.end(), boards_info.begin(), [](const auto& tup) {
            const auto& [fen, sign, l, t, c] = tup;
            int signed_l = sign == pgnparser_ast::NEGATIVE ? ~l : l;
            return std::make_tuple(signed_l, t, c, fen);
        });
        m = std::make_unique<multiverse_even>(boards_info, size_x, size_y);
    }
    else
    {
        std::transform(boards.begin(), boards.end(), boards_info.begin(), [](const auto& tup) {
            const auto& [fen, sign, l, t, c] = tup;
            int sgn = sign == pgnparser_ast::NEGATIVE ? -1 : 1;
            return std::make_tuple(l*sgn, t, c, fen);
        });
        m = std::make_unique<multiverse_odd>(boards_info, size_x, size_y);
    }
    std::tie(present, player) = m->get_present();
    // parse moves
    const pgnparser_ast::gametree *gt = &g.gt;
    while(!gt->variations.empty())
    {
        const auto &[act, last_gt] = *(gt->variations.end() - 1);
        //std::cout << act;
        for(const auto& mv: act.moves)
        {
            auto [fm_opt, pt_opt, candidates] = parse_move(mv);
            if(!fm_opt.has_value())
            {
                if(candidates.empty())
                {
                    std::ostringstream oss;
                    dprint(to_string());
                    oss << "state(): Invalid move: " << mv;
                    throw std::runtime_error(oss.str());
                }
                else
                {
                    std::ostringstream oss;
                    dprint(to_string());
                    oss << "state(): Ambiguous move: " << mv << "; candidates: ";
                    oss << range_to_string(candidates, "", "");
                    throw std::runtime_error(oss.str());
                }
            }
            else
            {
                full_move fm = fm_opt.value();
                bool flag;
                if(pt_opt.has_value())
                {
                    piece_t pt = player ? to_white(*pt_opt) : *pt_opt;
                    flag = apply_move<false>(fm, pt);
                }
                else
                {
                    flag = apply_move<false>(fm);
                }
                if(!flag)
                {
                    std::ostringstream oss;
                    oss << "state(): Illegal move: " << mv << " (parsed as: " << fm << ")";
                    throw std::runtime_error(oss.str());
                }
            }
        }
        if(!last_gt->variations.empty())
        {
            bool flag = submit();
            if(!flag)
            {
                std::ostringstream oss;
                oss << "state(): Cannot submit after parsing these moves: " << act;
                throw std::runtime_error(oss.str());
            }
        }
        else
        {
            bool flag = submit();
            if(!flag)
            {
                std::cerr << "[WARNING]state(): Cannot submit after parsing these moves: " << act;
            }
        }
        gt = last_gt.get();
    }
}

int state::new_line() const
{
    auto [l_min, l_max] = m->get_lines_range();
    if(player == 0)
        return l_max + 1;
    else
        return l_min - 1;
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

std::optional<state> state::can_apply(full_move fm, piece_t promote_to) const
{
    state new_state = *this;
    bool flag = new_state.apply_move<false>(fm, promote_to);
    if(flag)
    {
        return std::make_optional<state>(new_state);
    }
    else
    {
        return std::nullopt;
    }
}

std::optional<state> state::can_apply(const action &act) const
{
    state new_state = *this;
    for(const auto& em : act.get_moves())
    {
        bool flag = new_state.apply_move<false>(em.fm, em.promote_to);
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
bool state::apply_move(full_move fm, piece_t promote_to)
{
    dprint("applying move", fm);
    vec4 p = fm.from;
    vec4 q = fm.to;
    vec4 d = q - p;
    if constexpr (!UNSAFE)
    {
        auto te = m->get_timeline_end(p.l());
        assert(std::make_pair(p.t(), player) == te && "moves must be made on an active board");
        auto mvs = player ? m->gen_moves<true>(p) : m->gen_moves<false>(p);
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
    if(d.l() == 0 && d.t() == 0)
    {
        const std::shared_ptr<board>& b_ptr = m->get_board(p.l(), p.t(), player);
        bitboard_t z = pmask(p.xy());
        const auto &[size_x, size_y] = m->get_board_size();
        // en passant
        if((b_ptr->lpawn()&z) && d.x()!=0 && b_ptr->get_piece(q.xy()) == NO_PIECE)
        {
            dprint(" ... en passant");
            m->append_board(p.l(), b_ptr
                            ->replace_piece(ppos(q.x(),p.y()), NO_PIECE)
                            ->move_piece(p.xy(), q.xy()));
        }
        // promotion
        else if((b_ptr->lpawn()&z) && (q.y() == 0 || q.y() == size_y - 1))
        {
            dprint(" ... promotion");
            piece_t promoted = player ? to_black(promote_to) : promote_to;
            m->append_board(p.l(), b_ptr
                            ->replace_piece(p.xy(), NO_PIECE)
                            ->replace_piece(q.xy(), promoted));
        }
        // castling
        else if((b_ptr->king()&z) && abs(d.x()) > 1)
        {
            dprint(" ... castling");
            int rook_x1 = d.x() < 0 ? 0 : (size_x - 1); //rook's original x coordinate
            int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1); //rook's new x coordinate
            m->append_board(p.l(),b_ptr
                            ->move_piece(ppos(rook_x1, p.y()), ppos(rook_x2,q.y()))
                            ->move_piece(p.xy(), q.xy()));
        }
        // normal move
        else
        {
            dprint(" ... normal move/capture");
            m->append_board(p.l(), b_ptr->move_piece(p.xy(), q.xy()));
        }
    }
    // non-branching superphysical move
    else if (std::make_pair(q.t(), player) == m->get_timeline_end(q.l()))
    {
        const std::shared_ptr<board>& b_ptr = m->get_board(p.l(), p.t(), player);
        const piece_t& pic = static_cast<piece_t>(piece_name(b_ptr->get_piece(p.xy())));
        m->append_board(p.l(), b_ptr->replace_piece(p.xy(), NO_PIECE));
        
        bitboard_t z = pmask(p.xy());
        const auto &[size_x, size_y] = m->get_board_size();
        const std::shared_ptr<board>& c_ptr = m->get_board(q.l(), q.t(), player);
        
        // promotion (only brawns can do)
        if ((b_ptr->lrawn()&z) && (q.y() == 0 || q.y() == size_y - 1))
        {
            dprint(" ... nonbranching brawn promotion");
            piece_t promoted = player ? to_black(promote_to) : promote_to;
            m->append_board(q.l(), c_ptr->replace_piece(q.xy(), promoted));
        }
        // normal non_branching move
        else
        {
            dprint(" ... nonbranching jump");
            m->append_board(q.l(), c_ptr->replace_piece(q.xy(), pic));
        }
    }
    //branching move
    else
    {
        const std::shared_ptr<board>& b_ptr = m->get_board(p.l(), p.t(), player);
        const piece_t& pic = static_cast<piece_t>(piece_name(b_ptr->get_piece(p.xy())));
        m->append_board(p.l(), b_ptr->replace_piece(p.xy(), NO_PIECE));
        const std::shared_ptr<board>& x_ptr = m->get_board(q.l(), q.t(), player);
        auto [t, c] = next_turn({q.t(), player});
        
        bitboard_t z = pmask(p.xy());
        const auto &[size_x, size_y] = m->get_board_size();
        
        // promotion (only brawns can do)
        if ((b_ptr->lrawn()&z) && (q.y() == 0 || q.y() == size_y - 1))
        {
            dprint(" ... branching brawn promotion");
            piece_t promoted = player ? to_black(promote_to) : promote_to;
            m->insert_board(new_line(), t, c, x_ptr->replace_piece(q.xy(), promoted));
        }
        // normal non_branching move
        else
        {
            dprint(" ... branching jump");
            m->insert_board(new_line(), t, c, x_ptr->replace_piece(q.xy(), pic));
        }
        auto [new_present, _] = m->get_present();
        if(new_present < present)
        {
            // if a historical board is activated by this travel, go back
            present = new_present;
        }
    }
    return true;
}

state::move_info state::get_move_info(full_move fm, piece_t pt) const
{
    dprint("get_move_info", fm);
    std::optional<state> new_state = can_apply(fm, pt).and_then([](state s){
        const auto [l_min, l_max] = s.get_lines_range();
        for(int l = l_min; l <= l_max; l++)
        {
            auto [t,c] = s.get_timeline_end(l);
            if(c == !s.player)
            {
                s.m->append_board(l, s.m->get_board(l, t, c));
            }
        }
        return s;
    });
    vec4 new_pos(0,0,0,0);
    
    if(new_state)
    {
        vec4 p = fm.from;
        vec4 q = fm.to;
        vec4 d = q - p;
        
        /* WARNING: similiar logic used in hypercuboid.cpp for applying semimoves
         If some move logic needs to be changed here, make sure also perform change
         in HC_info::build_HC()
         */
        // physical move, no time travel
        if(d.l() == 0 && d.t() == 0)
        {
            const std::shared_ptr<board>& b_ptr = m->get_board(p.l(), p.t(), player);
            bitboard_t z = pmask(p.xy());
            const auto &[size_x, size_y] = m->get_board_size();
            
            // castling
            if((b_ptr->king()&z) && abs(d.x()) > 1)
            {
                dprint(" ... castling");
                int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1); //rook's new x coordinate
                //            m->append_board(p.l(),b_ptr
                //                            ->move_piece(ppos(rook_x1, p.y()), ppos(rook_x2,q.y()))
                //                            ->move_piece(p.xy(), q.xy()));
                new_pos = q + vec4(0,0,1,0);
                vec4 rook_pos = vec4(rook_x2, q.y(), q.t()+1, q.l());
                //find rook checks
                auto moves = player ? new_state->m->gen_moves<true>(rook_pos) : new_state->m->gen_moves<false>(rook_pos);
                for (const auto& [q0, bb] : moves)
                {
                    std::shared_ptr<board> b1_ptr = new_state->m->get_board(q0.l(), q0.t(), player);
                    if (bb)
                    {
                        bitboard_t c_pieces = bb & b1_ptr->royal();
                        if (c_pieces)
                        {
                            return {new_state, new_pos, true};
                        }
                    }
                }
            }
            // other kinds of move
            else
            {
                dprint(" ... other physical move");
                //            m->append_board(p.l(), b_ptr->move_piece(p.xy(), q.xy()));
                new_pos = q + vec4(0,0,1,0);
            }
        }
        // non-branching superphysical move
        else if (std::make_pair(q.t(), player) == m->get_timeline_end(q.l()))
        {
            dprint(" ... non-branching superphysical move");
            new_pos = q + vec4(0,0,1,0);
        }
        //branching move
        else
        {
            dprint(" ... branching superphysical move");
            new_pos = vec4(q.x(), q.y(), q.t()+1, new_line());
        }
        //find checks
        auto moves = player ? new_state->m->gen_moves<true>(new_pos) : new_state->m->gen_moves<false>(new_pos);
        for (const auto& [q0, bb] : moves)
        {
            std::shared_ptr<board> b1_ptr = new_state->m->get_board(q0.l(), q0.t(), player);
            if (bb)
            {
                bitboard_t c_pieces = bb & b1_ptr->royal();
                if (c_pieces)
                {
                    return {new_state, new_pos, true};
                }
            }
        }
    }
    return {new_state, new_pos, false};
}

template <bool UNSAFE>
bool state::submit()
{
    auto [t, c] = m->get_present();
    if constexpr (!UNSAFE)
    {
        if(player == c)
        {
            return false;
        }
    }
    present = t;
    player  = c;
    return true;
}

state state::phantom() const
{
    const auto [l_min, l_max] = get_lines_range();
    state s = *this;
    for(int l = l_min; l <= l_max; l++)
    {
        auto [t,c] = get_timeline_end(l);
        if(c == player)
        {
            s.m->append_board(l, m->get_board(l, t, c));
        }
    }
    return s;
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> state::get_timeline_status() const
{
    return get_timeline_status(present, player);
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> state::get_timeline_status(int present_t, bool present_c) const
{
    auto [l_min, l_max] = m->get_lines_range();
    auto [active_min, active_max] = m->get_active_range();
    turn_t present_tc = std::make_pair(present_t, present_c);
    std::vector<int> mandatory_timelines, optional_timelines, unplayable_timelines;
    for(int l = l_min; l <= l_max; l++)
    {
        turn_t tc = m->get_timeline_end(l);
        if (active_min <= l && active_max >= l && tc == present_tc)
        {
            mandatory_timelines.push_back(l);
        }
        else
        {
            auto [t, c] = tc;
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

generator<full_move> state::find_checks(bool c) const
{
    // cannot use get_timeline_status() directly because it only works for current player
    auto [l_min, l_max] = m->get_lines_range();
    auto [active_min, active_max] = m->get_active_range();
    std::vector<int> lines;
    auto [p_min, p_max] = c ? std::make_pair(active_min, l_max) : std::make_pair(l_min, active_max);
    for(int i = p_min; i <= p_max; i++)
    {
        auto [t, col] = m->get_timeline_end(i);
        if(col == c)
        {
            lines.push_back(i);
        }
    }
    if (c)
    {
        return find_checks_impl<true>(lines);
    }
    else
    {
        return find_checks_impl<false>(lines);
    }
}

template<bool C>
generator<full_move> state::find_checks_impl(std::vector<int> lines) const
{
//    print_range(__PRETTY_FUNCTION__, lines);
    for (int l : lines)
    {
        // take the active board
        auto [t, c] = m->get_timeline_end(l);
        assert(c == C);
        std::shared_ptr<board> b_ptr = m->get_board(l, t, C);
        bitboard_t b_pieces = b_ptr->friendly<C>() & ~b_ptr->wall();
        // for each friendly piece on this board
        for (int src_pos : marked_pos(b_pieces))
        {
            vec4 p = vec4(src_pos, vec4(0,0,t,l));
            // generate the aviliable moves
            auto moves = m->gen_moves<C>(p);
            // for each destination board and bit location
            for (const auto& [q0, bb] : moves)
            {
                std::shared_ptr<board> b1_ptr = m->get_board(q0.l(), q0.t(), C);
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


std::vector<vec4> state::gen_movable_pieces() const
{
    auto [mandatory_timelines, optional_timelines, unplayable_timelines] = get_timeline_status(present, player);
    auto lines = concat_vectors(mandatory_timelines, optional_timelines);
    return get_movable_pieces(lines);
}

std::vector<vec4> state::get_movable_pieces(std::vector<int> lines) const
{
    if (player == 0)
    {
        return gen_movable_pieces_impl<false>(lines);
    }
    else
    {
        return gen_movable_pieces_impl<true>(lines);
    }
}

template <bool C>
std::vector<vec4> state::gen_movable_pieces_impl(std::vector<int> lines) const
{
    dprint("gen_movable_pieces_impl()");
    std::vector<vec4> result;
    for (int l : lines)
    {
        // take the active board
        auto [t, c] = get_timeline_end(l);
        const vec4 p0 = vec4(0,0,t,l);
//        assert(c == C);
        std::shared_ptr<board> b_ptr = m->get_board(l, t, C);
        bitboard_t b_pieces = b_ptr->friendly<C>() & ~b_ptr->wall();
        // for each friendly piece on this board
        for (int src_pos : marked_pos(b_pieces))
        {
            vec4 p = vec4(src_pos, p0);
            // generate the aviliable moves
            auto moves = m->gen_moves<C>(p);
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

state::mate_type state::get_mate_type() const
{
    auto [w, ss] = HC_info::build_HC(*this);
    auto hc = ss.hcs.back();
    search_space ss1 {{hc}};
    ss.hcs.pop_back();
    if(w.search(ss1).first().has_value())
    {
        return mate_type::NONE;
    }
    bool soft = false;
    for(moveseq mvs : w.search(ss))
    {
        soft = true;
        for(full_move fm : mvs)
        {
            if((fm.to-fm.from).t() < 0)
            {
                return mate_type::NONE;
            }
        }
    }
    if(soft)
    {
        return mate_type::SOFTMATE;
    }
    if(phantom().find_checks(!player).first().has_value())
    {
        return mate_type::CHECKMATE;
    }
    else
    {
        return mate_type::STALEMATE;
    }
}

std::pair<int, int> state::get_board_size() const
{
    return m->get_board_size();
}


turn_t state::get_present() const
{
    return std::make_pair(present, player);
}

turn_t state::apparent_present() const
{
    return m->get_present();
}

std::pair<int, int> state::get_initial_lines_range() const
{
    return m->get_initial_lines_range();
}

std::pair<int, int> state::get_lines_range() const
{
    return m->get_lines_range();
}

std::pair<int, int> state::get_active_range() const
{
    return m->get_active_range();
}

turn_t state::get_timeline_start(int l) const
{
    return m->get_timeline_start(l);
}

turn_t state::get_timeline_end(int l) const
{
    return m->get_timeline_end(l);
}

piece_t state::get_piece(vec4 p, bool color) const
{
    return m->get_piece(p, color);
}

std::shared_ptr<board> state::get_board(int l, int t, bool c) const
{
    return m->get_board(l, t, c);
}

std::vector<std::tuple<int, int, bool, std::string>> state::get_boards() const
{
    return m->get_boards();
}

generator<vec4> state::gen_piece_move(vec4 p) const
{
    return m->gen_piece_move(p, player);
}

generator<vec4> state::gen_piece_move(vec4 p, bool c) const
{
    return m->gen_piece_move(p, c);
}

std::string state::to_string() const
{
    std::ostringstream ss;
    ss << "State(present=" << present << ", player=" << player << "):\n";
    return ss.str() + m->to_string();
}

std::string state::show_fen() const
{
    std::ostringstream oss;
    for(const auto &[l,t,c,s] : m->get_boards<true>())
    {
        oss << "[" << s << ":" << m->pretty_l(l);
        oss << ":" << t << ":" << (c?"b":"w") << "]\n";
    }
    return oss.str();
}

state::parse_pgn_res state::parse_move(const pgnparser_ast::move &move) const
{
    std::vector<full_move> matched;
    std::vector<full_move> pawn_move_matched;
    std::optional<full_move> fm;
    std::optional<piece_t> promotion;
    dprint("parse_move(",move,")");
    constexpr static uint16_t FLAGS = SHOW_PAWN | SHOW_CAPTURE | SHOW_PROMOTION;
    if(std::holds_alternative<pgnparser_ast::physical_move>(move.data))
    {
        auto mv = std::get<pgnparser_ast::physical_move>(move.data);
        // for all physical moves avilable in current state
        for(vec4 p : gen_movable_pieces())
        {
            char piece = to_white(piece_name(get_piece(p, player)));
            bitboard_t bb = player ? m->gen_physical_moves<true>(p) : m->gen_physical_moves<false>(p);
            for(int pos : marked_pos(bb))
            {
                vec4 q(pos, p.tl());
                full_move fm(p,q);
                dprint("matching", fm);
                // test if this physical move matches any of them
                std::string full_notation = pretty_move<FLAGS>(fm);
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
        for(vec4 p : gen_movable_pieces())
        {
            char piece = to_white(piece_name(get_piece(p, player)));
            auto gen = player ? m->gen_superphysical_moves<true>(p) : m->gen_superphysical_moves<false>(p);
            for(const auto& [p0, bb] : gen)
            {
                for(int pos : marked_pos(bb))
                {
                    vec4 q(pos, p0);
                    full_move fm(p,q);
                    dprint("matching", fm);
                    // test if this physical move matches any of them
                    std::string full_notation = pretty_move<FLAGS>(fm);
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
    auto parsed_move = pgnparser(move).parse_move();
    if(!parsed_move.has_value())
    {
        return std::make_tuple(std::nullopt, std::nullopt, std::vector<full_move>{});
    }
    return parse_move(*parsed_move);
}

template bool state::apply_move<false>(full_move, piece_t);
template bool state::apply_move<true>(full_move, piece_t);
template bool state::submit<false>();
template bool state::submit<true>();

template generator<full_move> state::find_checks_impl<false>(std::vector<int>) const;
template generator<full_move> state::find_checks_impl<true>(std::vector<int>) const;
template std::vector<vec4> state::gen_movable_pieces_impl<false>(std::vector<int>) const;
template std::vector<vec4> state::gen_movable_pieces_impl<true>(std::vector<int>) const;
