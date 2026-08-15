#include "linear.h"
#include "rollout.h"
#include "statistics.h"

float evaluation(state)
{
    return 0.0f;
}

float linear_engine::default_policy(state position, std::stop_token stop_token, std::mt19937 *rng)
{
    const std::optional<bool> winner = rollout(
        std::move(position),
        rollout_max_actions.load(),
        stop_token,
        rng);
    if(winner.has_value())
    {
        return *winner ? -WINNING_SCORE : WINNING_SCORE;
    }
    return evaluation(position);
}
