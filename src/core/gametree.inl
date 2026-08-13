// gametree.inl - Template implementations for gnode class

#ifndef GAMETREE_INL
#define GAMETREE_INL

#include "gametree.h"

template<typename T>
inline state gnode<T>::get_state()
{
    if(s)
    {
        return *s;
    }
    else if(parent)
    {
        s = *parent->get_state().can_apply(act);
        return *s;
    }
    else
    {
        throw std::runtime_error("gnode::get_state(): Root gnode has no state");
    }
}

template<typename T>
inline std::string gnode<T>::to_string(
    std::function<std::string(T)> show,
    pgn_options show_flags,
    turn_t start_turn,
    bool full_turn_display
)
{
    std::ostringstream oss;
    size_t num_children = children.size();
    if(parent) // non-root
    {
        auto [t, c] = start_turn;
        if(full_turn_display)
        {
            oss << t << (c?'b':'w')  << ". ";
        }
        else if(c)
        {
            oss << "/ ";
        }
        else
        {
            oss << t << ". ";
        }
        oss << act.pgn(parent->get_state(), show_flags) << " ";
        oss << show(info);
        start_turn = next_turn(start_turn);
        if(c && num_children > 0)
        {
            oss << '\n';
        }
    }
    else
    {
        oss << show(info) << "\n";
    }
    if((static_cast<uint16_t>(show_flags) & static_cast<uint16_t>(pgn_options::SHOW_OUTCOME))
       && outcome.has_value())
    {
        switch(*outcome)
        {
            case pgnparser_ast::WHITE_WINS:
                oss << "1-0";
                break;
            case pgnparser_ast::BLACK_WINS:
                oss << "0-1";
                break;
            case pgnparser_ast::DRAW:
                oss << "1/2-1/2";
                break;
            default:
                break;
        }
    }
    if(num_children > 1)
    {
        for(auto it = children.begin(); it+1 != children.end(); it++)
        {
            oss << "(" << (**it).to_string(show, show_flags, start_turn, true) << ")\n";
        }
    }
    if(num_children > 0)
    {
        auto it = (children.end() - 1);
        oss << (**it).to_string(show, show_flags, start_turn, num_children > 1);
    }
    return oss.str();
}

#endif /* GAMETREE_INL */
