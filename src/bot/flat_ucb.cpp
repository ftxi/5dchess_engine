#include "flat_ucb.h"

#include <cmath>
#include <limits>


double uct(double sum_reward, std::size_t visits, std::size_t parent_visits)
{
    if(visits == 0)
    {
        return std::numeric_limits<double>::infinity();
    }

    const double average_reward = sum_reward / static_cast<double>(visits);
    const double logParent = std::log(static_cast<double>(parent_visits) + 1.0);
    const double exploration = exploration_constant * std::sqrt(logParent / static_cast<double>(visits));
    return average_reward + exploration;
}
