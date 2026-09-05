#undef NDEBUG
#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>

#include "mcts_engines.h"

int main()
{
    static_assert(std::has_virtual_destructor_v<zero_engine>);
    using info_t = mcts_node_info<uct_tree_policy::node_data>;
    static_assert(!std::is_copy_constructible_v<info_t>);
    static_assert(!std::is_copy_assignable_v<info_t>);
    info_t child;
    assert(!child.registered);
    assert(!child.tree_policy_data.all_children_included);
    assert(!child.tree_policy_data.fully_expanded);
    assert(child.sum_reward == 0.0f);
    assert(child.visits == 0);

    const float white_score = uct(0.0f, 1, 100, true);
    const float black_score = uct(0.0f, 1, 100, false);
    // With zero average reward, only the exploration bonus remains. White
    // maximizes Q + U, while Black minimizes Q - U, so the scores must have
    // opposite signs and equal magnitude.
    assert(white_score > 0.0f);
    assert(black_score < 0.0f);
    assert(std::abs(white_score + black_score) < 1e-6f);

    // An unvisited child must be selected before any visited child: +infinity
    // wins White's maximization and -infinity wins Black's minimization.
    assert(uct(0.0f, 0, 1, true)
           == std::numeric_limits<float>::infinity());
    assert(uct(0.0f, 0, 1, false)
           == -std::numeric_limits<float>::infinity());
    return 0;
}
