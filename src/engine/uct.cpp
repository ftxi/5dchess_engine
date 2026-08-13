#include "uct.h"

#include <cmath>
#include <limits>

float uct(
    float sum_reward,
    std::size_t visits,
    std::size_t parent_visits,
    bool maximizing_player)
{
    if(visits == 0)
    {
        return maximizing_player
            ? std::numeric_limits<float>::infinity()
            : -std::numeric_limits<float>::infinity();
    }

    const float average_reward = sum_reward / static_cast<float>(visits);
    const float log_parent = std::log(static_cast<float>(parent_visits) + 1.0f);
    const float exploration = exploration_constant
        * std::sqrt(log_parent / static_cast<float>(visits));
    return maximizing_player
        ? average_reward + exploration
        : average_reward - exploration;
}
