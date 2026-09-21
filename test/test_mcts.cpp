#undef NDEBUG
#include <cassert>
#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>

#include "mcts_engines.h"

namespace
{

struct no_options {};

struct empty_options
{
    inline constexpr static auto watched_options = std::tuple{};
};

struct integer_options
{
    int value = 0;
    void set_shared(int new_value) { value = new_value; }

    inline constexpr static auto watched_options = std::tuple{
        policy_option{"shared", &integer_options::set_shared}
    };
};

struct real_options
{
    double value = 0.0;
    void set_shared(double new_value) { value = new_value; }

    inline constexpr static auto watched_options = std::tuple{
        policy_option{"shared", &real_options::set_shared}
    };
};

void test_option_dispatch()
{
    no_options absent;
    empty_options empty;
    integer_options first, second;
    real_options real;

    auto result = dispatch_watched_options(
        "shared", engine::option_value_t{7},
        absent, first, empty, second);
    assert(result.matched == 2);
    assert(result.dispatched == 2);
    assert(first.value == 7 && second.value == 7);

    result = dispatch_watched_options(
        "shared", engine::option_value_t{2.5}, first, real, second);
    assert(result.matched == 3);
    assert(result.dispatched == 1);
    assert(first.value == 7 && second.value == 7);
    assert(real.value == 2.5);

    result = dispatch_watched_options(
        "unknown", engine::option_value_t{1}, first, real);
    assert(result.matched == 0);
    assert(result.dispatched == 0);
}

void test_default_policy_options()
{
    static_assert(std::tuple_size_v<
        decltype(rollout_default_policy::watched_options)> == 1);
    static_assert(std::tuple_size_v<
        decltype(weighted_rollout_default_policy::watched_options)> == 2);

    using rollout_limit_option = std::tuple_element_t<
        0, decltype(rollout_default_policy::watched_options)>;
    using weight_temperature_option = std::tuple_element_t<
        1, decltype(weighted_rollout_default_policy::watched_options)>;
    static_assert(std::same_as<rollout_limit_option::value_type, int>);
    static_assert(std::same_as<weight_temperature_option::value_type, double>);
}

} /* anonymous namespace */

int main()
{
    test_option_dispatch();
    test_default_policy_options();
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
