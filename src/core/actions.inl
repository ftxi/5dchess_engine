#ifndef ACTIONS_INL
#define ACTIONS_INL

template<uint16_t OPTIONS>
std::string full_move::pgn(const state &s, piece_t pt) const
{
    char check_symbol = 0;
    if constexpr(OPTIONS & pgn_options::SHOW_MATE)
    {
        state::move_info mi = s.get_move_info(*this, pt);
        if(mi.checking_opponent)
        {
            check_symbol = '+';
        }
    }
    return pgn_impl<OPTIONS>(s, pt, check_symbol, false);
}

template<uint16_t OPTIONS>
std::string full_move::pgn_impl(const state &s, piece_t pt, char check_symbol, bool multimove) const
{
    static_assert((OPTIONS & ~pgn_options::SHOW_ALL) == 0, "Invalid options for full_move::pgn");
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
        if constexpr(OPTIONS & pgn_options::SHOW_PAWN)
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
        else if constexpr(!(OPTIONS & pgn_options::SHOW_PAWN))
        {
            if (pic==PAWN_W)
            {
                /* pawn captures include the file letter of the originating square
                of the capturing pawn immediately prior to the "x" character. */
                if((OPTIONS & pgn_options::SHOW_CAPTURE) && s.get_piece(q, player) != NO_PIECE)
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
            if constexpr(OPTIONS & pgn_options::SHOW_CAPTURE)
            {
                if(s.get_piece(q, player) != NO_PIECE)
                {
                    oss << "x";
                }
            }
            if(to_tl)
            {
                if constexpr(OPTIONS & pgn_options::SHOW_RELATIVE)
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
            if constexpr(OPTIONS & pgn_options::SHOW_CAPTURE)
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
    if constexpr(OPTIONS & pgn_options::SHOW_SHORT)
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
        //from: tl, file, rank; to: tl
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
    if constexpr(OPTIONS & pgn_options::SHOW_PROMOTION)
    {
        if((pic == PAWN_W || pic == BRAWN_W) && (q.y() == (player ? 0 : (s.get_board_size().second - 1))))
        {
            oss << "=" << pt;
        }
    }
    if constexpr(OPTIONS & pgn_options::SHOW_MATE)
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

template<uint16_t OPTIONS>
std::string ext_move::pgn(const state &s) const
{
    return fm.pgn<OPTIONS>(s, promote_to);
}

template<uint16_t OPTIONS>
std::string action::pgn(const state &initial_state) const
{
    state t = initial_state;
    std::vector<ext_move> mvs = get_moves();
    std::vector<char> check_symbols(mvs.size(), 0);
    state::mate_type mt = state::mate_type::NONE;
    if constexpr (OPTIONS & pgn_options::SHOW_MATE)
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
        pgn += m.pgn_impl<OPTIONS>(s, pt, check_symbols[i], multimove) + " ";
        s.apply_move<true>(m, pt);
    }
    if(!pgn.empty())
    {
        pgn.pop_back();
    }
    return pgn;
}

struct pgn_detail {
    using pgn_move_dispatch_fn = std::string (full_move::*)(const state &, piece_t) const;
    using pgn_action_dispatch_fn = std::string (action::*)(const state &) const;
    static constexpr std::size_t PGN_TABLE_SIZE = static_cast<std::size_t>(pgn_options::SHOW_ALL) + 1;

    template<std::size_t Options>
    static constexpr pgn_move_dispatch_fn pgn_move_dispatch_entry() noexcept;

    template<std::size_t Options>
    static constexpr pgn_action_dispatch_fn pgn_action_dispatch_entry() noexcept;

    template<std::size_t... Options>
    static constexpr std::array<pgn_move_dispatch_fn, PGN_TABLE_SIZE>
    make_pgn_move_dispatch_table(std::index_sequence<Options...>) noexcept;

    template<std::size_t... Options>
    static constexpr std::array<pgn_action_dispatch_fn, PGN_TABLE_SIZE>
    make_pgn_action_dispatch_table(std::index_sequence<Options...>) noexcept;

    static const std::array<pgn_move_dispatch_fn, PGN_TABLE_SIZE>
    pgn_move_dispatch_table;

    static const std::array<pgn_action_dispatch_fn, PGN_TABLE_SIZE>
    pgn_action_dispatch_table;
};

inline std::string full_move::pgn(const state &s, piece_t pt, uint16_t options) const
{
    const uint16_t normalized_options = options & pgn_options::SHOW_ALL;
    auto dispatcher = pgn_detail::pgn_move_dispatch_table[static_cast<std::size_t>(normalized_options)];
    return (this->*dispatcher)(s, pt);
}

inline std::string ext_move::pgn(const state &s, uint16_t options) const
{
    return fm.pgn(s, promote_to, options);
}

inline std::string action::pgn(const state &s, uint16_t options) const
{
    const uint16_t normalized_options = options & pgn_options::SHOW_ALL;
    auto dispatcher = pgn_detail::pgn_action_dispatch_table[static_cast<std::size_t>(normalized_options)];
    return (this->*dispatcher)(s);
}

template<std::size_t Options>
constexpr pgn_detail::pgn_move_dispatch_fn pgn_detail::pgn_move_dispatch_entry() noexcept
{
    return &full_move::pgn<static_cast<uint16_t>(Options)>;
}

template<std::size_t Options>
constexpr pgn_detail::pgn_action_dispatch_fn pgn_detail::pgn_action_dispatch_entry() noexcept
{
    return &action::pgn<static_cast<uint16_t>(Options)>;
}

template<std::size_t... Options>
constexpr std::array<pgn_detail::pgn_move_dispatch_fn, pgn_detail::PGN_TABLE_SIZE>
pgn_detail::make_pgn_move_dispatch_table(std::index_sequence<Options...>) noexcept
{
    return { pgn_move_dispatch_entry<Options>()... };
}

template<std::size_t... Options>
constexpr std::array<pgn_detail::pgn_action_dispatch_fn, pgn_detail::PGN_TABLE_SIZE>
pgn_detail::make_pgn_action_dispatch_table(std::index_sequence<Options...>) noexcept
{
    return { pgn_action_dispatch_entry<Options>()... };
}

inline const std::array<pgn_detail::pgn_move_dispatch_fn, pgn_detail::PGN_TABLE_SIZE>
pgn_detail::pgn_move_dispatch_table = make_pgn_move_dispatch_table(std::make_index_sequence<pgn_detail::PGN_TABLE_SIZE>{});

inline const std::array<pgn_detail::pgn_action_dispatch_fn, pgn_detail::PGN_TABLE_SIZE>
pgn_detail::pgn_action_dispatch_table = make_pgn_action_dispatch_table(std::make_index_sequence<pgn_detail::PGN_TABLE_SIZE>{});

#endif // ACTIONS_INL
