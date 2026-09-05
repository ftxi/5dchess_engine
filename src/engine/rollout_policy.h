#ifndef ROLLOUT_POLICY_H
#define ROLLOUT_POLICY_H

#include <utility>

#include "default_policy.h"
#include "move_info_evaluation.h"

struct rollout_details
{
    enum class termination { WHITE_WINS, BLACK_WINS, STALEMATE, ACTION_LIMIT };
    termination end;
    std::size_t num_actions;
};

struct random_action_selection
{
    std::optional<moveseq> operator()(
        const state &, std::stop_token, std::mt19937 *) const;
};

struct weighted_action_selection
{
    move_info_weights weights;
    float temperature;

    weighted_action_selection(
        move_info_weights weights = default_move_info_weights,
        float temperature = default_move_info_temperature)
        : weights{std::move(weights)}, temperature{temperature} {}

    std::optional<moveseq> operator()(
        const state &, std::stop_token, std::mt19937 *) const;
};

struct rollout_cutoff_evaluation
{
    using result_type = reward_t<rollout_details>;
    result_type mate_reward(std::optional<bool> winner, std::size_t length) const
    {
        using end = rollout_details::termination;
        return winner ? result_type{*winner ? -WINNING_SCORE : WINNING_SCORE,
                                    {*winner ? end::BLACK_WINS : end::WHITE_WINS, length}}
                      : result_type{0.0f, {end::STALEMATE, length}};
    }
    std::optional<result_type> cutoff_reward(
        state, std::size_t length, std::stop_token stop, std::mt19937 *) const
    {
        if(stop.stop_requested()) return std::nullopt;
        return result_type{0.0f, {rollout_details::termination::ACTION_LIMIT, length}};
    }
};

using rollout_default_policy = default_policy_t<rollout_details,
    random_action_selection, rollout_cutoff_evaluation>;
using weighted_rollout_default_policy = default_policy_t<rollout_details,
    weighted_action_selection, rollout_cutoff_evaluation>;

// No simulated actions, but terminal positions retain their actual outcome.
struct zero_default_policy
{
    using result_type = reward_t<rollout_details>;
    template<class Observer>
    std::optional<result_type> evaluate(state s, std::stop_token stop, Observer &)
    {
        auto moves = random_action_selection{}(s, stop, nullptr);
        if(stop.stop_requested()) return std::nullopt;
        rollout_cutoff_evaluation evaluation;
        if(!moves)
        {
            std::optional<bool> winner;
            if(s.get_mate_type() == mate_type::CHECKMATE)
                winner = !s.get_present().second;
            return evaluation.mate_reward(winner, 0);
        }
        return evaluation.cutoff_reward(std::move(s), 0, stop, nullptr);
    }
};

static_assert(ActionSelection<random_action_selection>);
static_assert(ActionSelection<weighted_action_selection>);
static_assert(CutoffEvaluation<rollout_cutoff_evaluation, rollout_details>);

#endif
