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
inline std::string gnode<T>::render_pgn_turn(
    const std::function<std::string(T)> &show,
    pgn_options show_flags,
    turn_t turn,
    bool full_turn_display,
    const action &witness,
    bool has_continuation
)
{
    std::ostringstream oss;
    auto [t, c] = turn;
    if(full_turn_display)
    {
        oss << t << (c ? 'b' : 'w') << ". ";
    }
    else if(c)
    {
        oss << "/ ";
    }
    else
    {
        oss << t << ". ";
    }

    auto [move_pgn, mt] = act.pgn_advanced(
        parent->get_state(), show_flags, witness
    );
    oss << move_pgn << " " << show(info);

    if(static_cast<bool>(show_flags & pgn_options::SHOW_OUTCOME) && mt.has_value())
    {
        switch(*mt)
        {
            case mate_type::CHECKMATE: oss << (c ? "0-1" : "1-0"); break;
            case mate_type::STALEMATE: oss << "1/2-1/2"; break;
            case mate_type::NONE:
            case mate_type::SOFTMATE: break;
        }
    }
    if(c && has_continuation)
    {
        oss << '\n';
    }
    return oss.str();
}

template<typename T>
inline std::string gnode<T>::pgn_tree(
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
        const action empty_witness;
        const action &witness = children.empty()
            ? empty_witness
            : children.back()->get_action();
        oss << render_pgn_turn(
            show, show_flags, start_turn, full_turn_display,
            witness, !children.empty()
        );
        start_turn = next_turn(start_turn);
    }
    else
    {
        oss << show(info) << "\n";
    }
    if(num_children > 1)
    {
        for(auto it = children.begin(); it+1 != children.end(); it++)
        {
            oss << "(" << (**it).pgn_tree(show, show_flags, start_turn, true) << ")\n";
        }
    }
    if(num_children > 0)
    {
        auto it = (children.end() - 1);
        oss << (**it).pgn_tree(show, show_flags, start_turn, num_children > 1);
    }
    return oss.str();
}

template<typename T>
inline std::string gnode<T>::pgn_path(
    std::function<std::string(T)> show,
    pgn_options show_flags
)
{
    std::vector<gnode<T>*> path;
    for(auto *node = this; node != nullptr; node = node->get_parent())
    {
        path.push_back(node);
    }
    std::reverse(path.begin(), path.end());

    std::ostringstream oss;
    oss << show(path.front()->get_info()) << "\n";
    turn_t start_turn = {1, false};
    const action empty_witness;
    for(size_t i = 1; i < path.size(); ++i)
    {
        auto *node = path[i];
        const action &witness = i + 1 < path.size()
            ? path[i + 1]->get_action()
            : empty_witness;
        oss << node->render_pgn_turn(
            show, show_flags, start_turn, false,
            witness, i + 1 < path.size()
        );
        start_turn = next_turn(start_turn);
    }
    return oss.str();
}

#endif /* GAMETREE_INL */
