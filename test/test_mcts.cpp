#undef NDEBUG
#include <cassert>
#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>
#include "mcts_engines.h"
#include <algorithm>
#include <set>
#include "move_evaluation.h"
#include "pgnparser.h"
#include <cstddef>
#include "linear.h"
#include "multiverse.h"
#include <iostream>

namespace mcts_tests
{
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

    engine::option_value_t integer_value;
    integer_value.emplace<int>(7);
    engine::option_value_t real_value;
    real_value.emplace<double>(2.5);
    engine::option_value_t unknown_value;
    unknown_value.emplace<int>(1);

    auto result = dispatch_watched_options(
        "shared", integer_value,
        absent, first, empty, second);
    assert(result.matched == 2);
    assert(result.dispatched == 2);
    assert(first.value == 7 && second.value == 7);

    result = dispatch_watched_options(
        "shared", real_value, first, real, second);
    assert(result.matched == 3);
    assert(result.dispatched == 1);
    assert(first.value == 7 && second.value == 7);
    assert(real.value == 2.5);

    result = dispatch_watched_options(
        "unknown", unknown_value, first, real);
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

int run()
{
    test_option_dispatch();
    test_default_policy_options();
    static_assert(std::has_virtual_destructor_v<zero_engine>);
    using info_t = mcts_node_info<uct_tree_policy<>::node_data>;
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
} /* namespace mcts_tests */

namespace mcts_policies_tests
{
struct observer {};

std::optional<move_score_table> build_score_table(
    const HC_info &info, const move_evaluation_weights &weights,
    std::stop_token stop = {})
{
    return move_evaluator(info, weights).build_score_table(stop);
}

std::optional<move_score_table> capture_weights_table(
    const HC_info &info, std::stop_token stop = {})
{
    return move_evaluator(info, capture_weights).build_score_table(stop);
}

std::optional<move_score_table> capture_check_weights_table(
    const HC_info &info, std::stop_token stop = {})
{
    return move_evaluator(info, capture_check_weights).build_score_table(stop);
}

state standard_position()
{
    return state::from_pgn<true>("[Board \"Standard - Turn Zero\"]\n");
}

void test_rollout()
{
    observer obs;
    for(unsigned seed : {1u, 7u, 91u})
    {
        rollout_default_policy first({}, {}, 2, seed);
        rollout_default_policy second({}, {}, 2, seed);
        auto result = first.evaluate(standard_position(), {}, obs);
        auto repeated = second.evaluate(standard_position(), {}, obs);
        assert(result && repeated);
        assert(result->data.num_actions == 2);
        assert(result->data.end == rollout_details::termination::ACTION_LIMIT);
        assert(result->score == repeated->score);
        assert(result->data.num_actions == repeated->data.num_actions);
        assert(result->data.end == repeated->data.end);
    }
    for(const auto &[player, fen, score] : {
        std::tuple{true, "k7/1Q6/2K5/8/8/8/8/8", 1.0f},
        std::tuple{false, "K7/1q6/2k5/8/8/8/8/8", -1.0f},
        std::tuple{true, "k7/2Q5/2K5/8/8/8/8/8", 0.0f}})
    {
        multiverse boards(timeline_layout::odd, {{0, 1, player, fen}});
        state s(boards);
        rollout_default_policy policy({}, {}, 1, 42);
        auto result = policy.evaluate(s, {}, obs);
        assert(result && result->score == score && result->data.num_actions == 0);
        auto zero = zero_default_policy{}.evaluate(s, {}, obs);
        assert(zero && zero->score == score);
    }
    rollout_default_policy cutoff({}, {}, 0, 42);
    auto result = cutoff.evaluate(standard_position(), {}, obs);
    assert(result && result->score == 0 && result->data.num_actions == 0);
    cutoff.set_max_actions(1);
    result = cutoff.evaluate(standard_position(), {}, obs);
    assert(result && result->data.num_actions == 1);
    std::stop_source stopped;
    stopped.request_stop();
    assert(!cutoff.evaluate(standard_position(), stopped.get_token(), obs));
    assert(!zero_default_policy{}.evaluate(standard_position(), stopped.get_token(), obs));
}

void test_weighted_action_selection()
{
    multiverse boards(timeline_layout::odd, {{0, 1, false, "3k####/4####/1p2####/KR2####/########/########/########/########"}});
    const state position(boards);

    move_evaluation_weights weights{};
    weights.values[move_evaluation_weights::PAWN_CAPTURE] = 20.0f;
    weighted_action_selection selector(weights, 1.0f);
    assert(selector.get_temperature() == 1.0f);

    std::mt19937 first_rng(1234), second_rng(1234);
    const auto first = selector(position, {}, &first_rng);
    const auto second = selector(position, {}, &second_rng);
    assert(first && first == second);

    int capture_count = 0;
    for(unsigned int seed = 0; seed < 100; ++seed)
    {
        std::mt19937 rng(seed);
        const auto selected = selector(position, {}, &rng);
        assert(selected);
        const action legal_action = action::from_moveseq(*selected, position);
        assert(position.can_apply(legal_action));
        capture_count += std::ranges::any_of(*selected, [&](const full_move &move) {
            return move.captured_piece(position) != NO_PIECE;
        });
    }
    assert(capture_count >= 99);

    // A zero profile gives every coordinate the same exponential weight.
    weighted_action_selection neutral({}, 1.0f);
    std::set<moveseq> neutral_choices;
    for(unsigned int seed = 0; seed < 12; ++seed)
    {
        std::mt19937 rng(seed);
        const auto selected = neutral(position, {}, &rng);
        assert(selected);
        assert(position.can_apply(action::from_moveseq(*selected, position)));
        neutral_choices.insert(*selected);
    }
    assert(neutral_choices.size() > 1);

    selector.set_temperature(600.0f);
    assert(selector.get_temperature() == 600.0f);
    for(float invalid : {
        0.0f,
        -1.0f,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    })
    {
        bool threw = false;
        try
        {
            selector.set_temperature(invalid);
        }
        catch(const std::invalid_argument &)
        {
            threw = true;
        }
        assert(threw);
        assert(selector.get_temperature() == 600.0f);
    }

    std::stop_source stopped;
    stopped.request_stop();
    assert(!selector(position, stopped.get_token(), nullptr));
}

void test_completion()
{
    observer obs;
    auto root = uct_tree_policy<>::node_t::make_root(standard_position());
    // Pre-materialize multiple legal branches: completion must randomize these,
    // independently of the HC ordering used to discover them.
    natural_HC_ordering natural;
    for(auto index : root->search(natural)) (void)index;
    assert(root->get_children().size() > 1);
    std::set<uct_tree_policy<>::node_t *> selected;
    for(unsigned seed = 0; seed < 32; ++seed)
    {
        uct_tree_policy<> a(seed), b(seed);
        auto *first = a.complete_to_ceiling(root.get(), {}, obs);
        auto *second = b.complete_to_ceiling(root.get(), {}, obs);
        assert(first && first->is_ceiling() && first == second);
        assert(!first->is_nodal());
        selected.insert(first);
    }
    assert(selected.size() > 1);
    uct_tree_policy<> policy(9);
    auto *ceiling = policy.complete_to_ceiling(root.get(), {}, obs);
    sum_backpropagation{}.backpropagate(ceiling, reward_t<>{0.5f, {}}, obs);
    assert(root->get_info().visits == 1);
    // Adopt all materialized children; adopting the completion must not erase
    // its reward or visits.
    bool adopted = false;
    for(std::size_t i = 0; i < root->get_children().size(); ++i)
        if(policy.select(root.get(), {}, obs) == ceiling) adopted = true;
    assert(adopted);
    assert(ceiling->get_info().visits == 1);
    assert(ceiling->get_info().sum_reward == 0.5f);
    std::stop_source stopped;
    stopped.request_stop();
    assert(!policy.select(root.get(), stopped.get_token(), obs));
    assert(!policy.complete_to_ceiling(root.get(), stopped.get_token(), obs));
}

void test_capture_ordering()
{
    multiverse boards(timeline_layout::odd, {{0, 1, false, "3k####/4####/1p2####/KR2####/########/########/########/########"}});
    const state position(boards);
    auto root = uct_tree_policy<scored_HC_ordering>::node_t::make_root(position);
    observer obs;
    uct_tree_policy<scored_HC_ordering> policy(42, capture_weights_table);
    auto *selected = policy.select(root.get(), {}, obs);
    assert(selected && selected->get_parent() == root.get());
    const auto scores = build_score_table(
        root->get_hc_info(), capture_weights);
    assert(scores && (*scores)[selected->get_n()][selected->get_i()]
        == 480.0f);
}

void test_capture_check_progressive_widening()
{
    // Rxb2 captures without check; Rd1 checks without capture.
    multiverse boards(timeline_layout::odd, {{0, 1, false, "3k####/4####/Kp2####/1R2####/########/########/########/########"}});
    const state position(boards);
    observer obs;
    using policy_t = progressive_widening_tree_policy<scored_HC_ordering>;
    auto root = policy_t::node_t::make_root(position);
    const auto scores = build_score_table(
        root->get_hc_info(), capture_check_weights);
    assert(scores);
    bool capture = false, check = false;
    for(index_t axis = 0; axis < root->get_hc_info().get_universe().dimension(); ++axis)
    {
        for(index_t coordinate : root->get_hc_info().get_universe()[axis])
        {
            capture |= (*scores)[axis][coordinate] == 480.0f;
            check |= (*scores)[axis][coordinate] == 300.0f;
        }
    }
    assert(capture && check);

    policy_t policy(42, default_progressive_widening_constant,
                    default_progressive_widening_alpha,
                    capture_check_weights_table);
    assert(policy.widening_limit(0) == 1);
    assert(policy.widening_limit(1) == 2);
    assert(policy.widening_limit(4) == 4);
    auto *selected = policy.select(root.get(), {}, obs);
    assert(selected && selected->get_parent() == root.get());
    assert((*scores)[selected->get_n()][selected->get_i()] == 480.0f);
    auto *ceiling = policy.complete_to_ceiling(selected, {}, obs);
    assert(ceiling && ceiling->is_ceiling());
    assert(position.can_apply(action::from_moveseq(ceiling->to_action(), position)));
}

void test_score_table_and_generic_policies()
{
    // Rxd2 both captures the pawn and checks the king on d4.
    multiverse boards(timeline_layout::odd, {{0, 1, false, "3k####/4####/3p####/K2R####/########/########/########/########"}});
    const state position(boards);
    auto [info, space] = HC_info::build_HC(position);
    (void)space;
    const auto scores = build_score_table(
        info, capture_check_weights);
    assert(scores);
    bool combined = false;
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
        for(index_t coordinate : info.get_universe()[axis])
            combined |= (*scores)[axis][coordinate]
                == 480.0f + 300.0f;
    assert(combined);

    std::mt19937 rng(7);
    assert(construct_hc_ordering<natural_HC_ordering>(info, rng));
    assert(construct_hc_ordering<random_HC_ordering>(info, rng));
    std::stop_source stopped;
    stopped.request_stop();
    assert(!construct_hc_ordering<scored_HC_ordering>(
        info, rng, stopped.get_token(), capture_check_weights_table));
    assert(!build_score_table(
        info, capture_check_weights, stopped.get_token()));

    observer obs;
    uct_tree_policy<natural_HC_ordering> natural(7);
    auto natural_root = uct_tree_policy<natural_HC_ordering>::node_t::make_root(position);
    auto *natural_child = natural.select(natural_root.get(), {}, obs);
    assert(natural_child);
    auto *natural_ceiling = natural.complete_to_ceiling(natural_child, {}, obs);
    assert(natural_ceiling && natural_ceiling->is_ceiling());

    using widening_t = progressive_widening_tree_policy<natural_HC_ordering>;
    auto widening_root = widening_t::node_t::make_root(position);
    widening_t::set_ordering(widening_root.get(), natural_HC_ordering{});
    widening_t widening(7);
    auto *widening_child = widening.select(widening_root.get(), {}, obs);
    assert(widening_child);
    auto *widening_ceiling = widening.complete_to_ceiling(widening_child, {}, obs);
    assert(widening_ceiling && widening_ceiling->is_ceiling());
    assert(position.can_apply(action::from_moveseq(
        widening_ceiling->to_action(), position)));

    // A precomputed ordering can be supplied even when its type has no
    // constructor that accepts HC_info.
    using scored_widening_t = progressive_widening_tree_policy<scored_HC_ordering>;
    auto scored_root = scored_widening_t::node_t::make_root(position);
    auto scored_table = build_score_table(
        scored_root->get_hc_info(), capture_check_weights);
    assert(scored_table);
    std::mt19937 scored_rng(7);
    scored_widening_t::set_ordering(scored_root.get(), scored_HC_ordering(
        scored_root->get_hc_info().get_universe(), *scored_table, scored_rng));
    scored_widening_t scored_policy(7);
    auto *scored_child = scored_policy.select(scored_root.get(), {}, obs);
    assert(scored_child && (*scored_table)[scored_child->get_n()][scored_child->get_i()]
        == 480.0f + 300.0f);

    auto fallback_root = scored_widening_t::node_t::make_root(position);
    auto *fallback_child = scored_policy.select(fallback_root.get(), {}, obs);
    assert(fallback_child);
}

void test_multiple_semimoves()
{
    state position = state::from_pgn<true>(R"(
[Mode "5D"]
[Board "Very Small - Open"]
1. Bb2+ / Nxb2
2. N>>xd3 / (1T1)Bc3+
3. Bb2
)");
    auto root = uct_tree_policy<>::node_t::make_root(position);
    observer obs;
    uct_tree_policy<> policy(42);
    bool saw_multiple = false;
    for(int iteration = 0; iteration < 30; ++iteration)
    {
        auto *node = policy.select(root.get(), {}, obs);
        auto *ceiling = policy.complete_to_ceiling(node, {}, obs);
        assert(ceiling && ceiling->is_ceiling());
        // Find the state at the beginning of this action, even after the tree
        // has descended into subsequent turns.
        auto *start = ceiling->get_parent();
        int semimoves = 1;
        while(!start->is_nodal())
        {
            ++semimoves;
            start = start->get_parent();
        }
        saw_multiple |= semimoves > 1;
        const state &before = start->get_hc_info().get_state();
        auto action = action::from_moveseq(ceiling->to_action(), before);
        assert(before.can_apply(action));
        sum_backpropagation{}.backpropagate(ceiling, reward_t<>{0.0f, {}}, obs);
    }
    assert(saw_multiple);
}

template<class Policy>
void check_search_progress(Policy policy)
{
    const state position = state::from_pgn<true>(R"(
[Mode "5D"]
[Board "Very Small - Open"]
1. Bb2+ / Nxb2
2. N>>xd3 / (1T1)Bc3+
3. Bb2
)");
    auto root = Policy::node_t::make_root(position);
    observer obs;
    bool searched_reply = false;
    for(int iteration = 0; iteration < 500; ++iteration)
    {
        auto *node = policy.select(root.get(), {}, obs);
        auto *ceiling = policy.complete_to_ceiling(node, {}, obs);
        assert(ceiling && ceiling->is_ceiling());
        std::size_t action_depth = 0;
        for(auto *ancestor = ceiling; ancestor; ancestor = ancestor->get_parent())
            action_depth += ancestor->is_ceiling();
        searched_reply |= action_depth > 1;
        auto reward = zero_default_policy{}.evaluate(
            detail::state_at_ceiling(*ceiling), {}, obs);
        assert(reward);
        sum_backpropagation{}.backpropagate(ceiling, *reward, obs);
    }
    // Previously duplicate-coordinate siblings trapped admission, so the
    // engine kept evaluating first-turn leaves instead of searching replies.
    assert(searched_reply);
    auto check_children = [&](auto &&self, auto *node) -> void {
        std::set<index_t> coordinates;
        for(auto *child : node->get_children())
        {
            assert(coordinates.insert(child->get_i()).second);
            assert(node->get_child(child->get_i()) == child);
            self(self, child);
        }
    };
    check_children(check_children, root.get());
}

void test_search_progress()
{
    for(unsigned seed : {7u, 42u})
    {
        check_search_progress(uct_tree_policy<natural_HC_ordering>{seed});
        check_search_progress(
            progressive_widening_tree_policy<natural_HC_ordering>{seed});
        check_search_progress(progressive_widening_tree_policy<scored_HC_ordering>{
            seed, default_progressive_widening_constant,
            default_progressive_widening_alpha, capture_check_weights_table});
    }
}

struct stopping_action_selection
{
    std::stop_source *source;
    std::optional<moveseq> operator()(const state &, std::stop_token, std::mt19937 *)
    {
        source->request_stop();
        return std::nullopt;
    }
};

void test_interrupted_action_selection()
{
    observer obs;
    std::stop_source source;
    default_policy_t<rollout_details, stopping_action_selection, rollout_cutoff_evaluation>
        policy({&source}, {}, 1);
    // A stopped action search is not mistaken for stalemate/checkmate.
    assert(!policy.evaluate(standard_position(), source.get_token(), obs));
}

struct promotion_action_selection
{
    full_move move;
    std::optional<moveseq> operator()(const state &, std::stop_token, std::mt19937 *)
    {
        return moveseq{move};
    }
};

struct check_promoted_state : rollout_cutoff_evaluation
{
    const state *expected;
    std::optional<result_type> cutoff_reward(
        state s, std::size_t length, std::stop_token stop, std::mt19937 *rng) const
    {
        assert(s.get_boards() == expected->get_boards());
        return rollout_cutoff_evaluation::cutoff_reward(std::move(s), length, stop, rng);
    }
};

void test_promotion_paths()
{
    observer obs;
    for(bool black : {false, true})
    for(auto rules : {promotion_options::QUEEN, promotion_options::KNIGHT,
                      promotion_options::NONE})
    {
        const full_move move(black ? "(0T1)a2a1" : "(0T1)a7a8");
        multiverse boards(timeline_layout::odd, {{0, 1, black, black
            ? "k7/8/8/8/8/8/p7/7K" : "7k/P7/8/8/8/8/8/K7"}});
        state before(boards, rules);
        state expected = before;
        assert(expected.apply_move(move));
        assert(expected.submit());
        default_policy_t<rollout_details, promotion_action_selection, check_promoted_state>
            policy({move}, {{}, &expected}, 1);
        assert(policy.evaluate(before, {}, obs));

        auto root = uct_tree_policy<>::node_t::make_root(before);
        bool found = false;
        for(auto index : root->search(natural_HC_ordering{}))
        {
            auto *ceiling = finetree_nearby_ceiling(root->get_child(index));
            if(ceiling->to_action() != moveseq{move}) continue;
            found = true;
            assert(detail::state_at_ceiling(*ceiling).get_boards() == expected.get_boards());
            ceiling->ignite();
            assert(detail::state_at_ceiling(*ceiling).get_boards() == expected.get_boards());
            break;
        }
        assert(found);
    }
}

struct sink_io : io_handler
{
    std::string read_line() override { return {}; }
    void write_line(const std::string &) override {}
    bool is_open() override { return false; }
};

template<class Engine>
void test_engine()
{
    Engine engine(std::make_unique<sink_io>(), 42, 1);
    engine.set_position("startpos", "");
    auto best = engine.find_best_move(1, std::nullopt, {});
    assert(best);
    assert(engine.get_current_state()->can_apply(*best));
    std::stop_source stopped;
    stopped.request_stop();
    assert(!engine.find_best_move(1, std::nullopt, stopped.get_token()));
}

int run()
{
    test_rollout();
    test_weighted_action_selection();
    test_promotion_paths();
    test_completion();
    test_capture_ordering();
    test_capture_check_progressive_widening();
    test_score_table_and_generic_policies();
    test_multiple_semimoves();
    test_search_progress();
    test_interrupted_action_selection();
    test_engine<mcts_engine>();
    test_engine<mcts_weighted_engine>();
    test_engine<zero_engine>();
    test_engine<zero_capture_engine>();

    zero_capture_check_pw_engine progressive(
        std::make_unique<sink_io>(), 42, 2.0, 0.5);
    progressive.set_position("startpos", "");
    assert(progressive.find_best_move(1, std::nullopt, {}));

    static_assert(std::is_base_of_v<zero_capture_pw_mcts_engine, mvvlva_engine>);
    mvvlva_engine mvvlva(std::make_unique<sink_io>(), 42, 2.0, 0.5);
    mvvlva.set_position("startpos", "");
    const auto best = mvvlva.find_best_move(1, std::nullopt, {});
    assert(best && mvvlva.get_current_state()->can_apply(*best));
    std::stop_source stopped;
    stopped.request_stop();
    assert(!mvvlva.find_best_move(1, std::nullopt, stopped.get_token()));

    static_assert(std::is_base_of_v<zero_capture_pw_mcts_engine, mvvlva_exposure_engine>);
    mvvlva_exposure_engine exposure(std::make_unique<sink_io>(), 42, 2.0, 0.5);
    exposure.set_position("startpos", "");
    const auto exposed_best = exposure.find_best_move(1, std::nullopt, {});
    assert(exposed_best && exposure.get_current_state()->can_apply(*exposed_best));
    assert(!exposure.find_best_move(1, std::nullopt, stopped.get_token()));
    return 0;
}
} /* namespace mcts_policies_tests */

namespace linear_tests
{
namespace
{

static_assert(std::is_base_of_v<linear_mcts_engine, linear_engine>);
static_assert(std::is_base_of_v<
    weighted_linear_mcts_engine, linear_weighted_engine>);

struct observer {};

state standard_position()
{
    return state::from_pgn<true>(R"(
[Board "Standard - Turn Zero"]
)");
}

void test_feature_layout()
{
    static_assert(linear_cutoff_evaluation::features_count == 64);
    const auto features = linear_cutoff_evaluation::extract_features(standard_position());

    assert(features[linear_cutoff_evaluation::bias_offset] == 1.0f);
    const std::array<float, linear_cutoff_evaluation::material_feature_count> expected_sum{
        16.0f, 4.0f, 6.0f, 6.0f, 2.0f, 2.0f, 2.0f, 2.0f
    };
    for(std::size_t i = 0; i < expected_sum.size(); ++i)
    {
        assert(features[linear_cutoff_evaluation::mandatory_material_sum_offset + i]
               == expected_sum[i]);
        assert(features[linear_cutoff_evaluation::mandatory_material_diff_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::optional_material_sum_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::optional_material_diff_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::unplayable_material_sum_offset + i]
               == 0.0f);
        assert(features[linear_cutoff_evaluation::unplayable_material_diff_offset + i]
               == 0.0f);
    }

    const std::array<float, linear_cutoff_evaluation::timeline_feature_count>
        expected_timelines{
            1.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f
        };
    for(std::size_t i = 0; i < expected_timelines.size(); ++i)
    {
        assert(features[linear_cutoff_evaluation::timeline_offset + i]
               == expected_timelines[i]);
    }

    const float expected_move_space = std::log(21.0f);
    assert(std::abs(
        features[linear_cutoff_evaluation::log_universe_volume_offset]
        - expected_move_space) < 1e-6f);
    assert(std::abs(
        features[linear_cutoff_evaluation::log_non_new_volume_offset]
        - expected_move_space) < 1e-6f);
}

void test_weights_and_perspective()
{
    linear_cutoff_evaluation::weight_vector_t weights{};
    weights[linear_cutoff_evaluation::bias_offset] = 1.0f;
    linear_cutoff_evaluation evaluation(weights);

    const state white_to_move = standard_position();
    assert(std::abs(evaluation.evaluate(white_to_move) - std::tanh(1.0f)) < 1e-6f);
    assert(evaluation.get_weights() == weights);

    const state black_to_move = state::from_pgn<true>(R"(
[Board "Standard - Turn Zero"]

1. e4
)");
    assert(std::abs(evaluation.evaluate(black_to_move) + std::tanh(1.0f)) < 1e-6f);
}

void test_builtin_weight_profiles()
{
    const auto hand_written = linear_cutoff_evaluation::default_weights();
    const auto trained = linear_cutoff_evaluation::trained_weights();

    assert(hand_written.size() == 64);
    assert(trained.size() == 64);
    assert(hand_written != trained);
    assert(hand_written[linear_cutoff_evaluation::mandatory_material_diff_offset] == 0.05f);
    assert(hand_written[linear_cutoff_evaluation::timeline_advantage_offset] == 0.25f);
    assert(hand_written[linear_cutoff_evaluation::log_universe_volume_offset] == 0.04f);
    assert(hand_written[linear_cutoff_evaluation::log_non_new_volume_offset] == 0.04f);
    assert(trained[linear_cutoff_evaluation::bias_offset] == -0.00123027945f);
    assert(trained[linear_cutoff_evaluation::log_universe_volume_offset] == 0.0140336938f);
    assert(trained[linear_cutoff_evaluation::log_non_new_volume_offset] == 0.0197210461f);

    assert(linear_cutoff_evaluation(hand_written).get_weights() == hand_written);
}

void test_rollout_policy_preserves_input()
{
    state original = standard_position();
    const turn initial_turn = original.get_present();
    observer obs;
    rollout_default_policy policy({}, {}, 1, 7);
    const auto result = policy.evaluate(original, {}, obs);
    assert(result);
    assert(result->data.end == rollout_details::termination::ACTION_LIMIT);
    assert(result->data.num_actions == 1);
    assert(original.get_present() == initial_turn);
}

void test_stalemate_rollout_termination()
{
    multiverse multiverse(timeline_layout::odd, {
        {0, 1, true, "k7/2Q5/2K5/8/8/8/8/8"}
    });
    observer obs;
    rollout_default_policy policy({}, {}, 1, 1);
    const auto result = policy.evaluate(state(multiverse), {}, obs);
    assert(result);
    assert(result->data.end == rollout_details::termination::STALEMATE);
    assert(result->data.num_actions == 0);
}

void test_winner_rollout_termination()
{
    multiverse white_win_multiverse(timeline_layout::odd, {
        {0, 1, true, "k7/1Q6/2K5/8/8/8/8/8"}
    });
    observer obs;
    rollout_default_policy policy({}, {}, 1, 1);
    const auto white_win = policy.evaluate(state(white_win_multiverse), {}, obs);
    assert(white_win);
    assert(white_win->data.end == rollout_details::termination::WHITE_WINS);
    assert(white_win->data.num_actions == 0);

    multiverse black_win_multiverse(timeline_layout::odd, {
        {0, 1, false, "K7/1q6/2k5/8/8/8/8/8"}
    });
    const auto black_win = policy.evaluate(state(black_win_multiverse), {}, obs);
    assert(black_win);
    assert(black_win->data.end == rollout_details::termination::BLACK_WINS);
    assert(black_win->data.num_actions == 0);
}

void test_policy_evaluates_final_rollout_state()
{
    linear_cutoff_evaluation::weight_vector_t weights{};
    weights[linear_cutoff_evaluation::bias_offset] = 1.0f;
    observer obs;
    linear_default_policy policy(
        random_action_selection{},
        linear_cutoff_evaluation{weights},
        1,
        11);
    const auto result = policy.evaluate(standard_position(), {}, obs);
    assert(result);
    assert(result->data.end == rollout_details::termination::ACTION_LIMIT);
    assert(std::abs(result->score + std::tanh(1.0f)) < 1e-6f);
}

} /* anonymous namespace */

int run()
{
    test_feature_layout();
    test_weights_and_perspective();
    test_builtin_weight_profiles();
    test_rollout_policy_preserves_input();
    test_stalemate_rollout_termination();
    test_winner_rollout_termination();
    test_policy_evaluates_final_rollout_state();
    return 0;
}
} /* namespace linear_tests */

int main()
{
    std::cerr << "Running mcts tests\n";
    if(const int result = mcts_tests::run(); result != 0)
        return result;
    std::cerr << "Running mcts_policies tests\n";
    if(const int result = mcts_policies_tests::run(); result != 0)
        return result;
    std::cerr << "Running linear tests\n";
    if(const int result = linear_tests::run(); result != 0)
        return result;
    return 0;
}
