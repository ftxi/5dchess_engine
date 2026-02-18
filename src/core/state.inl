
template<uint16_t FLAGS>
std::string state::pretty_move(full_move fm, piece_t pt) const
{
    char check_symbol = 0;
    if constexpr(FLAGS & SHOW_MATE)
    {
        state::move_info mi = get_move_info(fm, pt);
        if(mi.checking_opponent)
        {
            check_symbol = '+';
        }
    }
    return pretty_move_impl<FLAGS>(fm, pt, check_symbol, false);
}

template<uint16_t FLAGS>
std::string state::pretty_move_impl(full_move fm, piece_t pt, char check_symbol, bool multimove) const
{
    static_assert((FLAGS & ~SHOW_ALL) == 0, "Invalid FLAGS for pretty_move");
    std::ostringstream oss;
    vec4 p = fm.from, q = fm.to;
    piece_t pic = to_white(piece_name(get_piece(p, player)));
    auto display = [&](bool from_tl, bool from_file, bool from_rank, bool to_tl) -> std::string {
        std::ostringstream oss;
        if(from_tl)
        {
            oss << m->pretty_lt(p.tl());
        }
        else if (multimove)
        {
            oss << "(L" << m->pretty_l(p.l()) << ")";
        }
        else
        {
            auto [mandatory_timelines, optional_timelines, unplayable_timelines] = get_timeline_status();
            auto it = std::find(mandatory_timelines.begin(), mandatory_timelines.end(), p.l());
            /* if this timeline is the only mandatory timeline, omit it; 
            otherwise display it to avoid ambiguity */
            if(mandatory_timelines.size() > 1 || it == mandatory_timelines.end())
            {
                oss << "(L" << m->pretty_l(p.l()) << ")";
            }
        }
        if constexpr(FLAGS & SHOW_PAWN)
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
        else if constexpr(!(FLAGS & SHOW_PAWN) && (FLAGS & SHOW_CAPTURE))
        {
            /* pawn captures include the file letter of the originating square
            of the capturing pawn immediately prior to the "x" character. */
            if(pic==PAWN_W && get_piece(q, player) != NO_PIECE)
            {
                oss << static_cast<char>(p.x() + 'a');
            }
        }
        if(from_rank)
        {
            oss << static_cast<char>(p.y() + '1');
        }
        if(p.tl() != q.tl())
        {
            //        std::cout << "p=" << p << "\t q=" << q << "\t";        // superphysical move
            if(std::pair{q.t(), player} < get_timeline_end(q.l()))
            {
                oss << ">>";
            }
            else
            {
                oss << ">";
            }
            if constexpr(FLAGS & SHOW_CAPTURE)
            {
                if(get_piece(q, player) != NO_PIECE)
                {
                    oss << "x";
                }
            }
            if(to_tl)
            {
                if constexpr(FLAGS & SHOW_RELATIVE)
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
                    oss << m->pretty_lt(q.tl());
                }
            }
        }
        else
        {
            //physical move
            if constexpr(FLAGS & SHOW_CAPTURE)
            {
                if(get_piece(q, player) != NO_PIECE)
                {
                    oss << "x";
                }
            }
        }
        oss << static_cast<char>(q.x() + 'a') << static_cast<char>(q.y() + '1');
        return oss.str();
    };
    if constexpr(FLAGS & SHOW_SHORT)
    {
        bool success = false;
        /* policy: try hide everything first, if not successful,
           1. display source TL; if fails again
           2. display source and target TL; if fails
           3. in addition, display file or rank
           4. display everything (no need to check correctness anymore)
         */
        std::vector<std::tuple<bool, bool, bool, bool>> attempts = {
        //from: tl, file, rank; to: tl
            {false, false, false, false},
            {true,  false, false, false},
            {true,  false, false, true},
            {true,  true,  false, true},
            {true,  false, true,  true},
        };
        for(const auto [from_tl, from_file, from_rank, to_tl] : attempts)
        {
            std::string mv_str = display(from_tl, from_file, from_rank, to_tl);
            //check this move has no ambiguity
            auto res = parse_move(mv_str);
            auto mv_opt = std::get<0>(res);
            if(mv_opt.has_value())
            {
                success = true;
                oss << mv_str;
                break;
            }
        }
        if(!success)
        {
            oss << display(true, true, true, true);
        }
    }
    else
    {
        oss << display(true, true, true, true);
    }
    if constexpr(FLAGS & SHOW_PROMOTION)
    {
        if((pic == PAWN_W) && (q.y() == (player ? 0 : (m->get_board_size().second - 1))))
        {
            oss << "=" << pt;
        }
    }
    if constexpr(FLAGS & SHOW_MATE)
    {
        /* display all checks here */
        /* if this is not a check, pretty_move/pretty_action will set the
        check symbol to 0 */ 
        if(check_symbol)
        {
            oss << check_symbol;
        }
    }
    return oss.str();
}

template<uint16_t FLAGS>
std::string state::pretty_action(action act) const
{
    state t = *this;
    std::vector<ext_move> mvs = act.get_moves();
    std::vector<char> check_symbols(mvs.size(), 0);
    mate_type mt = mate_type::NONE;
    if constexpr (FLAGS & SHOW_MATE)
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
            case mate_type::NONE:
                mate_symbol = '+';
                break;
            case mate_type::SOFTMATE:
                mate_symbol = '*';
                break;
            case mate_type::CHECKMATE:
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
    state s = *this;
    std::string pgn = "";
    bool multimove = mvs.size() > 1;
    for(size_t i = 0; i < mvs.size(); i++)
    {
        auto [m, pt] = mvs[i];
        pgn += s.pretty_move_impl<FLAGS>(m, pt, check_symbols[i], multimove) + " ";
        s.apply_move<true>(m, pt);
    }
    if(!pgn.empty())
    {
        pgn.pop_back();
    }
    return pgn;
}

struct state::detail {
    using pretty_move_dispatch_fn = std::string (state::*)(full_move, piece_t) const;
    using pretty_action_dispatch_fn = std::string (state::*)(action) const;
    static constexpr std::size_t PRETTY_MOVE_TABLE_SIZE = static_cast<std::size_t>(SHOW_ALL) + 1;

    template<std::size_t Flags>
    static constexpr pretty_move_dispatch_fn pretty_move_dispatch_entry() noexcept;

    template<std::size_t Flags>
    static constexpr pretty_action_dispatch_fn pretty_action_dispatch_entry() noexcept;

    template<std::size_t... Flags>
    static constexpr std::array<pretty_move_dispatch_fn, PRETTY_MOVE_TABLE_SIZE>
    make_pretty_move_dispatch_table(std::index_sequence<Flags...>) noexcept;

    template<std::size_t... Flags>
    static constexpr std::array<pretty_action_dispatch_fn, PRETTY_MOVE_TABLE_SIZE>
    make_pretty_action_dispatch_table(std::index_sequence<Flags...>) noexcept;

    static const std::array<pretty_move_dispatch_fn, PRETTY_MOVE_TABLE_SIZE>
    pretty_move_dispatch_table;

    static const std::array<pretty_action_dispatch_fn, PRETTY_MOVE_TABLE_SIZE>
    pretty_action_dispatch_table;
};

inline std::string state::pretty_move(full_move fm, piece_t pt, uint16_t flags) const
{
    const uint16_t normalized_flags = flags & SHOW_ALL;
    auto dispatcher = detail::pretty_move_dispatch_table[static_cast<std::size_t>(normalized_flags)];
    return (this->*dispatcher)(fm, pt);
}

inline std::string state::pretty_action(action act, uint16_t flags) const
{
    const uint16_t normalized_flags = flags & SHOW_ALL;
    auto dispatcher = detail::pretty_action_dispatch_table[static_cast<std::size_t>(normalized_flags)];
    return (this->*dispatcher)(act);
}

template<std::size_t Flags>
constexpr state::detail::pretty_move_dispatch_fn state::detail::pretty_move_dispatch_entry() noexcept
{
    return &state::pretty_move<static_cast<uint16_t>(Flags)>;
}

template<std::size_t Flags>
constexpr state::detail::pretty_action_dispatch_fn state::detail::pretty_action_dispatch_entry() noexcept
{
    return &state::pretty_action<static_cast<uint16_t>(Flags)>;
}

template<std::size_t... Flags>
constexpr std::array<state::detail::pretty_move_dispatch_fn, state::detail::PRETTY_MOVE_TABLE_SIZE>
state::detail::make_pretty_move_dispatch_table(std::index_sequence<Flags...>) noexcept
{
    return { pretty_move_dispatch_entry<Flags>()... };
}

template<std::size_t... Flags>
constexpr std::array<state::detail::pretty_action_dispatch_fn, state::detail::PRETTY_MOVE_TABLE_SIZE>
state::detail::make_pretty_action_dispatch_table(std::index_sequence<Flags...>) noexcept
{
    return { pretty_action_dispatch_entry<Flags>()... };
}

inline const std::array<state::detail::pretty_move_dispatch_fn, state::detail::PRETTY_MOVE_TABLE_SIZE>
state::detail::pretty_move_dispatch_table = make_pretty_move_dispatch_table(std::make_index_sequence<state::detail::PRETTY_MOVE_TABLE_SIZE>{});

inline const std::array<state::detail::pretty_action_dispatch_fn, state::detail::PRETTY_MOVE_TABLE_SIZE>
state::detail::pretty_action_dispatch_table = make_pretty_action_dispatch_table(std::make_index_sequence<state::detail::PRETTY_MOVE_TABLE_SIZE>{});
