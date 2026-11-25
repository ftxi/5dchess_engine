#undef NDEBUG
#include <cassert>
#include <set>
#include "neural.h"

namespace
{
using end = rollout_details::termination;

state position(std::string board, char mover = 'w')
{
    return state::from_pgn<true>("[Size \"4x4\"]\n[Board \"custom\"]\n[" + board
        + ":0:1:" + mover + "]\n");
}

// Independent small-board oracle: enumerate all legal turns, submit each,
// and classify the resulting check, without using the candidate ordering.
float verify_one_ply(const state &s, bool expect_double_capture = false)
{
    neural_model model;
    auto baseline = model.evaluate_position(s, {});
    assert(baseline && baseline->data.end == end::ACTION_LIMIT);
    bool double_capture = false;
    bool checked = s.has_phantom_check();
    float expected = baseline->score;
    std::set<std::string> seen;
    auto [info, space] = HC_info::build_HC(s);
    for(const auto &action : info.search(std::move(space)))
    {
        state next = s;
        unsigned captures = 0;
        for(const auto &move : action)
        {
            captures += move.captured_piece(next) != NO_PIECE;
            next.apply_move<true>(move);
        }
        next.submit<true>();
        double_capture |= captures >= 2;
        if(!checked && !captures && !next.has_phantom_check()) continue;
        const auto child = model.evaluate_position(next, {});
        assert(child);
        if(checked && seen.empty()) expected = child->score;
        expected = s.get_present().c() ? std::min(expected, child->score)
                                       : std::max(expected, child->score);
        seen.insert(next.show_fen());
    }
    quiescence_stats stats;
    auto actual = evaluate_quiescence(model, s, 1, {}, stats);
    assert(actual && actual->data.end == baseline->data.end);
    assert(actual->score == expected);
    assert(stats.successors == seen.size());
    assert(stats.deepest <= 1);
    assert(!expect_double_capture || double_capture);
    return actual->score;
}

} /* anonymous namespace */

int main()
{
    verify_one_ply(position("3k/4/1p2/KR2"));
    verify_one_ply(position("kr2/1P2/4/3K", 'b'));
    verify_one_ply(position("3k/4/4/K2r"));
    verify_one_ply(position("k3/4/B3/R2K")); // Bishop uncovers the rook.

    // Neither physical semimove checks in isolation. Once both boards advance,
    // Ra2 checks the other timeline's king along the timeline axis.
    verify_one_ply(state::from_pgn<true>("[Size \"4x4\"]\n[Board \"custom\"]\n"
        "[####/####/1###/R###:0:1:w]\n[###K/###1/k###/####:1:1:w]"));

    verify_one_ply(state::from_pgn<true>("[Size \"4x4\"]\n[Board \"custom\"]\n"
        "[k###/####/###p/K##R:0:1:w]\n[k###/####/###p/K##R:1:1:w]"), true);

    assert(verify_one_ply(position("k3/2Q1/2K1/4")) == WINNING_SCORE);
    assert(verify_one_ply(position("4/2k1/2q1/K3", 'b')) == -WINNING_SCORE);

    neural_model model;
    const auto tactical = position("3k/1p2/1R2/K3");
    quiescence_stats stats;
    auto result = evaluate_quiescence(model, tactical, 4, {}, stats);
    assert(result && stats.deepest == 4);
    assert(stats.successors <= 16 && stats.queries <= 64 && stats.work <= 2048);
    const auto mate_in_one = neural_detail::evaluate_position(
        &model, 0.1f, position("k3/2Q1/2K1/4"), {}, 1);
    assert(mate_in_one && mate_in_one->score == 0.1f && mate_in_one->data.end == end::ACTION_LIMIT);
    const auto mate = neural_detail::evaluate_position(
        &model, 0.1f, position("k3/1Q2/2K1/4", 'b'), {}, 4);
    assert(mate && mate->score == WINNING_SCORE && mate->data.end == end::WHITE_WINS);
    // The one-ply MLP misses Black's mating reply to White's evasions.
    const auto threatened = position("3q/2k1/4/RK1r");
    assert(threatened.has_phantom_check());
    assert(verify_one_ply(threatened) == 0.0f);
    const auto answered = evaluate_quiescence(model, threatened, 2, {}, stats);
    assert(answered && answered->score == -WINNING_SCORE && answered->data.end == end::ACTION_LIMIT);
    assert(evaluate_quiescence(model, threatened, 4, {}, stats));
    assert(stats.successors <= 16 && stats.successors >= 12);
    std::stop_source cancelled;
    cancelled.request_stop();
    assert(!evaluate_quiescence(model, tactical, 4, cancelled.get_token(), stats));
    assert(stats.successors == 0);
}
