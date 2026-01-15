
template<uint16_t FLAGS>
std::string state::pretty_move(full_move fm, piece_t pt) const
{
    char check_symbol = 0;
    if constexpr(FLAGS & SHOW_MATE)
    {
        state::move_info mi = get_move_info(fm, pt);
        check_symbol = mi.checking_opponent && '+';
    }
    return pretty_move_impl<FLAGS>(fm, pt, check_symbol);
}

template<uint16_t FLAGS>
std::string state::pretty_move_impl(full_move fm, piece_t pt, char check_symbol) const
{
    static_assert((FLAGS & ~SHOW_ALL) == 0, "Invalid FLAGS for pretty_move");
    std::ostringstream oss;
    vec4 p = fm.from, q = fm.to;
    oss << m->pretty_lt(p.tl());
    piece_t pic = to_white(piece_name(get_piece(p, player)));
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
    oss << static_cast<char>(p.x() + 'a') << static_cast<char>(p.y() + '1');
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
            show_diff(d.x());
            oss << "T";
            show_diff(d.y());
            oss << ")";
        }
        else
        {
            oss << m->pretty_lt(q.tl());
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
    size_t last_check_pos = mvs.size();
    for(size_t i = 0; i < mvs.size(); i++)
    {
        auto [m, pt] = mvs[i];
        state::move_info mi = t.get_move_info(m, pt);
        if(!mi.new_state)
            return "---INVALID ACTION---";
        if(mi.checking_opponent)
            last_check_pos = i;
        t = *mi.new_state;
    }
    char mate_symbol;
    switch (get_mate_type()) {
        case mate_type::NONE:
            mate_symbol = '+';
            break;
        case mate_type::SOFTMATE:
            mate_symbol = '*';
            break;
        case mate_type::CHECKMATE:
            mate_symbol = '#';
        default:
            break;
    }
    state s = *this;
    std::string pgn = "";
    for(size_t i = 0; i < mvs.size(); i++)
    {
        auto [m, pt] = mvs[i];
        char check_symbol = (i==last_check_pos) ? mate_symbol : '+';
        pgn += s.pretty_move_impl<state::SHOW_CAPTURE>(m, pt, check_symbol) + " ";
        s.apply_move<true>(m, pt);
    }
    if(!pgn.empty())
    {
        pgn.pop_back();
    }
    return pgn;
}
