#include "ast.h"
#include "utils.h"

namespace pgnparser_ast {

std::ostream &operator<<(std::ostream &os, const relative_board &rb)
{
    return os << "rb{ld:" << (rb.line_difference ? std::to_string(*rb.line_difference) : "?")
                << ",td:" << (rb.time_difference ? std::to_string(*rb.time_difference) : "?") << "}";
}

std::ostream &operator<<(std::ostream &os, const absolute_board &ab)
{
    return os << "ab{s:" << (ab.sign == POSITIVE ? "+" : ab.sign == NEGATIVE ? "-" : std::to_string(ab.sign))
                << ",l:" << (ab.line ? std::to_string(*ab.line) : "?")
                << ",t:" << (ab.time ? std::to_string(*ab.time) : "?") << "}";
}

std::ostream &operator<<(std::ostream &os, const physical_move &pm)
{
    os << "pm{b:";
    if(pm.board) {os << *pm.board;} else {os << "?";}
    if(pm.castle==CASTLE_KINGSIDE) return os << ",castle_kingside}";
    if(pm.castle==CASTLE_QUEENSIDE) return os << ",castle_queenside}";
    os << ",pn:" << (pm.piece_name ? *pm.piece_name : '?');
    os << ",ff:" << (pm.from_file ? *pm.from_file : '?');
    os << ",fr:" << (pm.from_rank ? std::to_string(*pm.from_rank) : "?");
    os << ",cp:" << (pm.capture ? "yes" : "no");
    os << ",tf:" << pm.to_file << ",tr:" << static_cast<int>(pm.to_rank);
    os << ",pt:" << (pm.promote_to ? *pm.promote_to : '?')  << "}";
    return os;
}

std::ostream &operator<<(std::ostream &os, const superphysical_move &sm)
{
    os << "sm{fb:";
    if(sm.from_board) {os << *sm.from_board;} else {os << "?";}
    os << ",pn:" << (sm.piece_name ? *sm.piece_name : '?');
    os << ",ff:" << (sm.from_file ? *sm.from_file : '?');
    os << ",fr:" << (sm.from_rank ? std::to_string(*sm.from_rank) : "?");
    os << ",ji:" << (sm.jump_indicater == NON_BRANCH_JUMP ? ">" :
                        sm.jump_indicater == BRANCHING_JUMP ? ">>" : "?");
    os << ",cp:" << (sm.capture ? "yes" : "no");
    os << ",tb:";
    if(std::holds_alternative<absolute_board>(sm.to_board))
        os << std::get<absolute_board>(sm.to_board);
    else if(std::holds_alternative<relative_board>(sm.to_board))
        os << std::get<relative_board>(sm.to_board);
    else
        os << "?";
    os << ",tf:" << sm.to_file << ",tr:" << static_cast<int>(sm.to_rank);
    os << ",pt:" << (sm.promote_to ? *sm.promote_to : '?')  << "}";
    return os;
}

std::ostream &operator<<(std::ostream &os, const move &mv)
{
    os << "mv:";
    if(std::holds_alternative<physical_move>(mv.data))
        os << std::get<physical_move>(mv.data);
    else
        os << std::get<superphysical_move>(mv.data);
    return os;
}

std::ostream &operator<<(std::ostream &os, const actions &ac)
{
    os << "action{moves:";
    os << range_to_string(ac.moves, "[", "]");
    os << ", comments:";
    os << range_to_string(ac.comments, "[", "]");
    os << "}";
    return os;
}

std::ostream &operator<<(std::ostream &os, const gametree &gt)
{
    os << "gametree[";
    if(std::holds_alternative<gametree::variations_t>(gt.variations_or_outcome))
    {
        bool first = true;
        for(const auto& [a, v] : std::get<gametree::variations_t>(gt.variations_or_outcome))
        {
            if(!first)
            {
                os << ",";
            }
            first = false;
            os << "(" << a << "," << *v << ")";
        }
    }
    else
    {
        token_t outcome = std::get<token_t>(gt.variations_or_outcome);
        os << "outcome:";
        switch(outcome)
        {
            case WHITE_WINS: os << "1-0"; break;
            case BLACK_WINS: os << "0-1"; break;
            case DRAW: os << "1/2-1/2"; break;
            default: os << static_cast<int>(outcome);
        }
    }
    os << "]";
    return os;
}

std::ostream &operator<<(std::ostream &os, const game &g)
{
    os << "game{header:[";
    bool first = true;
    for(auto [k,v]: g.headers)
    {
        if(first)
            first = false;
        else
            os << ",";
        os << "{k:" << k << ",v:" << v << "}";
    }
    os << "],boards:[";
    first = true;
    for(auto [fen, sgn, l, t, c]: g.boards)
    {
        if(first)
            first = false;
        else
            os << ",";
        os << "{fen:" << fen << ",sgn:" << (sgn == POSITIVE ? "+" : sgn == NEGATIVE ? "-" : std::to_string(sgn));
        os << ",l:" << l << ",t:" << t << ",c:" << (c ? "b" : "w") << "}";
    }
    os << "],gt:" << g.gt << "}";
    return os;
}

} // namespace
