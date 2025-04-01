#include "state.h"
#include <algorithm>
#include <ranges>
#include "utils.h"

state::state(multiverse mtv) : m(mtv)
{
    std::tie(present, player) = m.get_present();
}

int state::new_line() const
{
    if(player == 0)
        return m.l_max + 1;
    else
        return m.l_min - 1;
}

bool state::can_submit() const
{
    auto [present_t, present_c] = m.get_present();
//    std::cout << present_t << " " << present_c << std::endl;
//    std::cout << present << " " << player << std::endl;
//    std::cout << (player != present_c) << (present != present_t);
    return player != present_c || present != present_t;
}

template<bool UNSAFE>
bool state::apply_move(full_move fm)
{
    bool flag;
    std::visit(overloads {
        [&](std::monostate)
        {
            // can present shift to opponent?
            if(!can_submit())
            {
                flag = false;
            }
            else
            {
                auto [present_t, present_c] = m.get_present();
                present = present_t;
                player  = present_c;
                flag = true;
            }
        },
        [&](std::tuple<vec4, vec4> data)
        {
            auto [p, d] = data;
            vec4 q = p+d;
            if constexpr (!UNSAFE)
            {
                std::vector<vec4> moves = m.gen_piece_move(p, player);
                // is this a viable move? (not counting checking)
                if(!std::ranges::contains(moves, d))
                {
                    flag = false;
                    return;
                }
            }

            // physical move, no time travel
            if(d.l() == 0 && d.t() == 0)
            {
                const std::shared_ptr<board>& b_ptr = m.get_board(p.l(), p.t(), player);
                piece_t pic = b_ptr->get_piece(p.xy());
                // en passant
                if(to_white(pic) == PAWN_W && d.x()!=0 && b_ptr->get_piece(q.xy()) == NO_PIECE)
                {
                    //std::cout << " ... en passant";
                    m.append_board(p.l(),
                                   b_ptr->replace_piece(ppos(q.x(),p.y()), NO_PIECE)
                             ->move_piece(p.xy(), q.xy()));
                }
                // TODO: promotion
                // castling
                else if(to_white(pic) == KING_W && abs(d.x()) > 1)
                {
                    //std::cout << " ... castling";
                    int rook_x1 = d.x() < 0 ? 0 : 7;
                    int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1);
                    m.append_board(p.l(),b_ptr
                                   ->move_piece(ppos(rook_x1, p.y()), ppos(rook_x2,q.y()))
                        ->move_piece(p.xy(), q.xy()));
                }
                // normal move
                else
                {
                    //std::cout << " ... normal move/capture";
                    m.append_board(p.l(), b_ptr->move_piece(p.xy(), q.xy()));
                }
            }
            // non-branching superphysical move
            else if(multiverse::tc_to_v(q.t(), player) == m.timeline_end[multiverse::l_to_u(q.l())])
            {
                //std::cout << " ... nonbranching move";
                const std::shared_ptr<board>& b_ptr = m.get_board(p.l(), p.t(), player);
                const piece_t& pic = static_cast<piece_t>(piece_name(b_ptr->get_piece(p.xy())));
                m.append_board(p.l(), b_ptr->replace_piece(p.xy(), NO_PIECE));
                const std::shared_ptr<board>& c_ptr = m.get_board(q.l(), q.t(), player);
                m.append_board(q.l(), c_ptr->replace_piece(q.xy(), pic));
            }
            //branching move
            else
            {
                //std::cout << " ... branching move";
                const std::shared_ptr<board>& b_ptr = m.get_board(p.l(), p.t(), player);
                const piece_t& pic = static_cast<piece_t>(piece_name(b_ptr->get_piece(p.xy())));
                m.append_board(p.l(), b_ptr->replace_piece(p.xy(), NO_PIECE));
                const std::shared_ptr<board>& x_ptr = m.get_board(q.l(), q.t(), player);
                auto [t, c] = multiverse::v_to_tc(multiverse::tc_to_v(q.t(), player)+1);
                m.insert_board(new_line(), t, c, x_ptr->replace_piece(q.xy(), pic));
                auto [new_present, _] = m.get_present();
                if(new_present < present)
                {
                    // if an historical board is activated by this travel, go back
                    present = new_present;
                }
            }
            flag = true;
        }
    }, fm.data);
    return flag;
}

template bool state::apply_move<false>(full_move);
template bool state::apply_move<true>(full_move);

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> state::get_timeline_status() const
{
    std::vector<int> mandatory_timelines, optional_timelines, unplayable_timelines;
    int present_v = multiverse::tc_to_v(present, player);
    for(int l = m.l_min; l <= m.l_max; l++)
    {
        int v = m.timeline_end[multiverse::l_to_u(l)];
        if (std::max(m.l_min, -m.number_activated()) <= l
            && std::min(m.l_max, m.number_activated()) >= l
            && v == present_v)
        {
            mandatory_timelines.push_back(l);
        }
        else
        {
            auto [t, c] = multiverse::v_to_tc(v);
            if(player==c)
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
