#undef NDEBUG
#include <cassert>
#include <set>
#include "mcts_engines.h"
#include "pgnparser.h"
#include "rollout.h"

struct observer {};

state standard_position()
{
    return state(*pgnparser("[Board \"Standard - Turn Zero\"]\n").parse_game());
}

void test_rollout()
{
    observer obs;
    for(unsigned seed : {1u, 7u, 91u})
    {
        std::mt19937 rng(seed);
        auto expected = rollout(standard_position(), 2, {}, &rng);
        rollout_default_policy policy({}, {}, 2, seed);
        auto result = policy.evaluate(standard_position(), {}, obs);
        assert(result);
        assert(result->data.num_actions == expected.num_actions);
        assert(static_cast<int>(result->data.end) == static_cast<int>(expected.end));
    }
    for(const auto &[player, fen, score] : {
        std::tuple{true, "k7/1Q6/2K5/8/8/8/8/8", 1.0f},
        std::tuple{false, "K7/1q6/2k5/8/8/8/8/8", -1.0f},
        std::tuple{true, "k7/2Q5/2K5/8/8/8/8/8", 0.0f}})
    {
        multiverse_odd boards({{0, 1, player, fen}});
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

void test_completion()
{
    observer obs;
    auto root = uct_tree_policy::node_t::make_root(standard_position());
    // Pre-materialize multiple legal branches: completion must randomize these,
    // independently of the HC ordering used to discover them.
    natural_HC_ordering natural;
    for(auto index : root->search(natural)) (void)index;
    assert(root->get_children().size() > 1);
    std::set<uct_tree_policy::node_t *> selected;
    for(unsigned seed = 0; seed < 32; ++seed)
    {
        uct_tree_policy a(seed), b(seed);
        auto *first = a.complete_to_ceiling(root.get(), {}, obs);
        auto *second = b.complete_to_ceiling(root.get(), {}, obs);
        assert(first && first->is_ceiling() && first == second);
        assert(!first->is_nodal());
        selected.insert(first);
    }
    assert(selected.size() > 1);
    uct_tree_policy policy(9);
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

void test_multiple_semimoves()
{
    state position(*pgnparser(R"(
[Mode "5D"]
[Board "Very Small - Open"]
1. Bb2+ / Nxb2
2. N>>xd3 / (1T1)Bc3+
3. Bb2
)").parse_game());
    auto root = uct_tree_policy::node_t::make_root(position);
    observer obs;
    uct_tree_policy policy(42);
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
        const state &before = start->get_context()->hc_info.s;
        auto action = action::from_moveseq(ceiling->to_action(), before);
        assert(before.can_apply(action));
        sum_backpropagation{}.backpropagate(ceiling, reward_t<>{0.0f, {}}, obs);
    }
    assert(saw_multiple);
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
        multiverse_odd boards({{0, 1, black, black
            ? "k7/8/8/8/8/8/p7/7K" : "7k/P7/8/8/8/8/8/K7"}});
        state before(boards, rules);
        state expected = before;
        assert(expected.apply_move(move));
        assert(expected.submit());
        default_policy_t<rollout_details, promotion_action_selection, check_promoted_state>
            policy({move}, {{}, &expected}, 1);
        assert(policy.evaluate(before, {}, obs));

        auto root = uct_tree_policy::node_t::make_root(before);
        bool found = false;
        for(auto index : root->search(natural_HC_ordering{}))
        {
            auto *ceiling = root->get_child(index)->get_nearby_ceiling();
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

int main()
{
    test_rollout();
    test_promotion_paths();
    test_completion();
    test_multiple_semimoves();
    test_interrupted_action_selection();
    test_engine<mcts_engine>();
    test_engine<zero_engine>();
}
