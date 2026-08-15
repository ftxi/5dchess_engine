#include "action.h"
#include <cstdio>
#include <sstream>
#include "utils.h"
#include "state.h"

full_move::full_move(std::string str): from{0,0,0,0}, to{0,0,0,0}
{
    // LAN may include a piece letter after the first timeline coordinate.
    const auto first_close = str.find(')');
    if(first_close != std::string::npos && first_close + 1 < str.size()
       && str[first_close + 1] >= 'A' && str[first_close + 1] <= 'Z')
    {
        str.erase(first_close + 1, 1);
    }

    // Normalize the optional timeline separator ("", ">", or ">>").
    const auto second_open = str.find('(', first_close == std::string::npos ? 0 : first_close + 1);
    if(second_open != std::string::npos)
    {
        auto separator_begin = second_open;
        while(separator_begin > 0 && str[separator_begin - 1] == '>')
        {
            --separator_begin;
        }
        str.erase(separator_begin, second_open - separator_begin);
    }

    int l1 = 0, t1 = 0, x1 = 0, y1 = 0;
    int l2 = 0, t2 = 0, x2 = 0, y2 = 0;
    char from_x, from_y, to_x, to_y;
    int consumed = -1;
    const auto valid_square = [](char file, char rank) {
        return file >= 'a' && file <= 'h' && rank >= '1' && rank <= '8';
    };

    const bool physical = std::sscanf(
        str.c_str(), "(%dT%d)%c%c%c%c%n",
        &l1, &t1, &from_x, &from_y, &to_x, &to_y, &consumed) == 6
        && consumed == static_cast<int>(str.size())
        && valid_square(from_x, from_y) && valid_square(to_x, to_y);

    if(physical)
    {
        l2 = l1;
        t2 = t1;
        x1 = static_cast<int>(from_x - 'a');
        y1 = static_cast<int>(from_y - '1');
        x2 = static_cast<int>(to_x - 'a');
        y2 = static_cast<int>(to_y - '1');
    }
    else
    {
        consumed = -1;
        if(std::sscanf(
               str.c_str(), "(%dT%d)%c%c(%dT%d)%c%c%n",
               &l1, &t1, &from_x, &from_y, &l2, &t2, &to_x, &to_y,
               &consumed) != 8
           || consumed != static_cast<int>(str.size())
           || !valid_square(from_x, from_y) || !valid_square(to_x, to_y))
        {
            throw std::runtime_error("Cannot match this move in any known pattern: " + str);
        }
        x1 = static_cast<int>(from_x - 'a');
        y1 = static_cast<int>(from_y - '1');
        x2 = static_cast<int>(to_x - 'a');
        y2 = static_cast<int>(to_y - '1');
    }

    from = vec4(x1, y1, t1, l1);
    to = vec4(x2, y2, t2, l2);
}

std::string full_move::to_string() const
{
    std::ostringstream os;
    vec4 p = from, q = to;
    vec4 d = q - p;
    if(d.t() == 0 && d.l() == 0)
    {
        os << '(' << p.l() << 'T' << p.t() << ')' << (char)(p.x()+'a') << (char)(p.y()+'1') << (char)(q.x()+'a') << (char)(q.y()+'1');
    }
    else
    {
        os << '(' << p.l() << 'T' << p.t() << ')' << (char)(p.x()+'a') << (char)(p.y()+'1') << "(" << q.l() << 'T' << q.t() << ')' << (char)(q.x()+'a') << (char)(q.y()+'1');
    }
    return os.str();
}

std::string full_move::lan(const state &s, piece_t promote_to) const
{
    auto [present, player] = s.get_present();
    piece_t pic = to_white(piece_name(s.get_piece(from, player)));
    std::ostringstream os;
    os << s.pretty_lt(from.tl()) << pic
       << static_cast<char>(from.x() + 'a') << static_cast<char>(from.y() + '1')
       << s.pretty_lt(to.tl())
       << static_cast<char>(to.x() + 'a') << static_cast<char>(to.y() + '1');
    if((pic == PAWN_W || pic == BRAWN_W)
        && to.y() == (player ? 0 : (s.get_board_size().second - 1)))
    {
        os << promote_to;
    }
    return os.str();
}

bool full_move::operator<(const full_move &other) const
{
    return std::tie(from, to) < std::tie(other.from, other.to); 
}

bool full_move::operator==(const full_move &other) const
{
    return std::tie(from, to) == std::tie(other.from, other.to);
}

std::ostream &operator<<(std::ostream &os, const full_move &fm)
{
    os << fm.to_string();
    return os;
}

/*********************************/

ext_move::ext_move(std::string s)
    : fm(s.empty() || s.back() < 'A' || s.back() > 'Z'
             ? s : s.substr(0, s.size() - 1)),
      promote_to(s.empty() || s.back() < 'A' || s.back() > 'Z'
             ? QUEEN_W : static_cast<piece_t>(s.back()))
{
}

std::string ext_move::to_string() const
{
    return fm.to_string() + static_cast<char>(promote_to);
}

std::string ext_move::lan(const state &s) const
{
    return fm.lan(s, promote_to);
}

/*********************************/

int action::sort(std::vector<ext_move> &mvs, const state &s)
{
    size_t rbranching_index = 0;
    auto [present, player] = s.get_present();
    std::set<int> moved_lines;
    for(size_t i=0; i<mvs.size(); i++)
    {
        vec4 p = mvs[i].fm.from;
        vec4 q = mvs[i].fm.to;
        auto tc1 = std::make_pair(q.t(), player);
        auto tc2 = s.get_timeline_end(q.l());
        bool branching =  tc1 < tc2 || (tc1 == tc2 && moved_lines.contains(q.l()));
        moved_lines.insert(p.l());
        if(branching)
        {
            std::swap(mvs[i], mvs[rbranching_index]);
            rbranching_index++;
        }
        else
        {
            moved_lines.insert(q.l());
        }
    }
    if(rbranching_index < mvs.size())
    {
        int sign = player ? -1 : 1;
        std::sort(mvs.begin()+rbranching_index, mvs.end(), [sign](ext_move m1, ext_move m2){
            return sign*m1.get_to().l() < sign*m2.get_to().l();
        });
        std::rotate(mvs.begin(), mvs.begin()+rbranching_index, mvs.end());
    }
    return static_cast<int>(mvs.size() - rbranching_index);
}

action action::from_vector(const std::vector<ext_move> &mvs, const state &s)
{
    action a{mvs};
    a.branching_index = sort(a.mvs, s);
    return a;
}

action action::from_moveseq(const moveseq &mvs, const state &s)
{
    std::vector<ext_move> ext_mvs;
    ext_mvs.reserve(mvs.size());
    std::transform(mvs.begin(), mvs.end(), std::back_inserter(ext_mvs),
                   [](const auto& fm) { return ext_move(fm); });
    return action::from_vector(ext_mvs, s);
}

std::string action::to_string() const
{
    std::ostringstream os;
    for(const auto &mv : mvs)
    {
        os << mv.to_string() << " ";
    }
    return os.str();
}

std::string action::lan(const state &initial_state) const
{
    state s = initial_state;
    std::string result;
    for(const auto &mv : mvs)
    {
        result += mv.lan(s) + " ";
        s.apply_move<true>(mv.fm, mv.promote_to);
    }
    if(!result.empty())
    {
        result.pop_back();
    }
    return result;
}

std::ostream& operator<<(std::ostream &os, const action &act)
{
    os << act.to_string();
    return os;
}

/*********************************/


std::string full_move::pgn(const state &s, piece_t pt, pgn_options options) const
{
    options &= pgn_options::SHOW_ALL;
    char check_symbol = 0;
    if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_MATE))
    {
        state::move_info mi = s.get_move_info(*this, pt);
        if(mi.checking_opponent)
        {
            check_symbol = '+';
        }
    }
    return pgn_impl(s, pt, options, check_symbol, false);
}

std::string full_move::pgn_impl(const state &s, piece_t pt, pgn_options options,
                                char check_symbol, bool multimove) const
{
    std::ostringstream oss;
    vec4 p = from, q = to;
    bool player = s.get_present().second;
    piece_t pic = to_white(piece_name(s.get_piece(p, player)));
    auto display_from_tl = [&](bool from_tl) -> std::string {
        std::ostringstream oss;
        if(from_tl)
        {
            oss << s.pretty_lt(p.tl());
        }
        else if (multimove)
        {
            oss << "(L" << s.pretty_l(p.l()) << ")";
        }
        else
        {
            auto [mandatory_timelines, optional_timelines, unplayable_timelines] = s.get_timeline_status();
            auto it = std::find(mandatory_timelines.begin(), mandatory_timelines.end(), p.l());
            /* if this timeline is the only mandatory timeline, omit it;
            otherwise display it to avoid ambiguity */
            if(mandatory_timelines.size() > 1 || it == mandatory_timelines.end())
            {
                oss << "(L" << s.pretty_l(p.l()) << ")";
            }
        }
        return oss.str();
    };

    auto display_rest = [&](bool from_file, bool from_rank, bool to_tl) -> std::string {
        std::ostringstream oss;
        if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_PAWN))
        {
            oss << pic;
        }
        else
        {
            if(pic != PAWN_W)
            {
                oss << pic;
            }
        }
        if(from_file)
        {
            oss << static_cast<char>(p.x() + 'a');
        }
        else if(!(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_PAWN)))
        {
            if (pic==PAWN_W)
            {
                /* pawn captures include the file letter of the originating square
                of the capturing pawn immediately prior to the "x" character. */
                if((static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_CAPTURE)) && s.get_piece(q, player) != NO_PIECE)
                {
                    oss << static_cast<char>(p.x() + 'a');
                }
                /* pawn jump should include the file letter */
                else if ((p-q).tl()!=vec4(0,0,0,0))
                {
                    oss << static_cast<char>(p.x() + 'a');
                }
            }
        }
        if(from_rank)
        {
            oss << static_cast<char>(p.y() + '1');
        }
        if(p.tl() != q.tl())
        {
            //        std::cout << "p=" << p << "\t q=" << q << "\t";        // superphysical move
            if(std::pair{q.t(), player} < s.get_timeline_end(q.l()))
            {
                oss << ">>";
            }
            else
            {
                oss << ">";
            }
            if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_CAPTURE))
            {
                if(s.get_piece(q, player) != NO_PIECE)
                {
                    oss << "x";
                }
            }
            if(to_tl)
            {
                if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_RELATIVE))
                {
                    vec4 d = q - p;
                    auto show_diff = [&oss](int w){
                        if(w>0)
                            oss << "+" << w;
                        else if(w<0)
                            oss << "-" << (-w);
                        else
                            oss << "=";
                    };
                    oss << "$(L";
                    show_diff(d.l());
                    oss << "T";
                    show_diff(d.t());
                    oss << ")";
                }
                else
                {
                    oss << s.pretty_lt(q.tl());
                }
            }
        }
        else
        {
            //physical move
            if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_CAPTURE))
            {
                if(s.get_piece(q, player) != NO_PIECE)
                {
                    oss << "x";
                }
            }
        }
        oss << static_cast<char>(q.x() + 'a') << static_cast<char>(q.y() + '1');
        return oss.str();
    };
    if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_SHORT))
    {
        bool success = false;
        /* policy: try hide everything first, if not successful,
            1. display source file; if fails again
            2. display source rank; if fails
            3. display source TL while hiding file and rank; if fails
            4. display source and target TL; if fails
            5. in addition, display file or rank
            6. display everything (no need to check correctness anymore)
         */
        constexpr static auto attempts = std::array{
            //        from: tl, file, rank; to: tl
            std::tuple{false, false, false, false},
            std::tuple{false, true,  false, false},
            std::tuple{false, false, true,  false},
            std::tuple{true,  false, false, false},
            std::tuple{true,  false, false, true},
            std::tuple{true,  true,  false, true},
            std::tuple{true,  false, true,  true},
        };
        for(const auto &[from_tl, from_file, from_rank, to_tl] : attempts)
        {
            std::string tl_part = display_from_tl(from_tl);
            std::string rest_part = display_rest(from_file, from_rank, to_tl);
            std::string mv_str = tl_part + rest_part;
            //check this move has no ambiguity
            auto res = s.parse_move(mv_str);
            auto mv_opt = std::get<0>(res);
            if(mv_opt.has_value())
            {
                success = true;
                /* extra work for castling in standard chessboard:
                 Replace Ke1g1 and Ke8g8 with O-O
                 Replace Ke1c1 and Ke8c8 with O-O-O */
                if(pic == KING_W && std::abs(q.x() - p.x()) == 2
                && (q.y() == 0 || q.y() == 7)
                && q.y() == p.y() && q.tl() == p.tl() )
                {
                    if(q.x() == 6)
                    {
                        mv_str = tl_part + "O-O";
                    }
                    else if(q.x() == 2)
                    {
                        mv_str = tl_part + "O-O-O";
                    }
                }
                oss << mv_str;
                break;
            }
        }
        if(!success)
        {
            oss << display_from_tl(true) << display_rest(true, true, true);
        }
    }
    else
    {
        oss << display_from_tl(true) << display_rest(true, true, true);
    }
    if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_PROMOTION))
    {
        if((pic == PAWN_W || pic == BRAWN_W) && (q.y() == (player ? 0 : (s.get_board_size().second - 1))))
        {
            oss << "=" << pt;
        }
    }
    if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_MATE))
    {
        /* display all checks here */
        /* if this is not a check, full_move::pgn/action::pgn will set the
        check symbol to 0 */
        if(check_symbol)
        {
            oss << check_symbol;
        }
    }
    return oss.str();
}

std::string ext_move::pgn(const state &s, pgn_options options) const
{
    return fm.pgn(s, promote_to, options);
}

std::string action::pgn(const state &initial_state, pgn_options options) const
{
    options &= pgn_options::SHOW_ALL;
    state t = initial_state;
    std::vector<ext_move> mvs = get_moves();
    std::vector<char> check_symbols(mvs.size(), 0);
    state::mate_type mt = state::mate_type::NONE;
    if(static_cast<uint16_t>(options) & static_cast<uint16_t>(pgn_options::SHOW_MATE))
    {
        for(size_t i = 0; i < mvs.size(); i++)
        {
            auto [m, pt] = mvs[i];
            state::move_info mi = t.get_move_info(m, pt);
            if(!mi.new_state)
                return "---INVALID ACTION---";
            if(mi.checking_opponent)
            {
                check_symbols[i] = '+';
            }
            t = std::move(*mi.new_state);
        }
        bool flag = t.submit();
        if(!flag)
            return "---INVALID ACTION---";
        char mate_symbol;
        mt = t.get_mate_type();
        switch (mt)
        {
            case state::mate_type::NONE:
                mate_symbol = '+';
                break;
            case state::mate_type::SOFTMATE:
                mate_symbol = '*';
                break;
            case state::mate_type::CHECKMATE:
                mate_symbol = '#';
                break;
            default:
                mate_symbol = '?';
                break;
        }
        auto it = std::find(check_symbols.rbegin(), check_symbols.rend(), '+');
        if (it != check_symbols.rend())
        {
            *it = mate_symbol;
        }
    }
    state s = initial_state;
    std::string pgn = "";
    bool multimove = mvs.size() > 1;
    for(size_t i = 0; i < mvs.size(); i++)
    {
        auto [m, pt] = mvs[i];
        pgn += m.pgn_impl(s, pt, options, check_symbols[i], multimove) + " ";
        s.apply_move<true>(m, pt);
    }
    if(!pgn.empty())
    {
        pgn.pop_back();
    }
    return pgn;
}
