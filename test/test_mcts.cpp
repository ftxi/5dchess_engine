#undef NDEBUG
#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>

#include "mcts.h"

int main()
{
    // Fine-tree child payloads are default-constructed and initialized when
    // MCTS adopts them; search statistics must never be copied.
    static_assert(!std::is_copy_constructible_v<mcts_node_info>);
    static_assert(!std::is_copy_assignable_v<mcts_node_info>);
    static_assert(std::has_virtual_destructor_v<zero_engine>);
    mcts_node_info parent;
    parent.is_included = true;
    parent.all_children_included = true;
    parent.fully_expanded = true;
    parent.sum_reward = 42.0f;
    parent.visits = 7;

    mcts_node_info child;
    // Turn ownership belongs to fine_node's state, not MCTS metadata. Search
    // statistics and expansion bookkeeping are node-local and must not be
    // copied from the parent, otherwise the new child appears explored.
    assert(!child.is_included);
    assert(!child.all_children_included);
    assert(!child.fully_expanded);
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
