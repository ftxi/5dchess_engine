#ifndef MCTS_ENGINES_H
#define MCTS_ENGINES_H

#include <iomanip>
#include <sstream>
#include "uct.h"
#include "backpropagation.h"
#include "selection_policy.h"
#include "rollout_policy.h"

inline constexpr int default_mcts_rollout_max_actions = 200;

struct mcts_observer
{
    std::size_t conclusive = 0, inconclusive = 0, terminal = 0;
    std::string output;
    void reset() { *this = {}; }

    template<class Node, class Result>
    void watch(const event::iteration_completed<Node, Result> &ev)
    {
        if(!ev.reward) return;
        const auto &data = ev.reward->data;
        if(data.end == rollout_details::termination::ACTION_LIMIT) ++inconclusive;
        else if(data.num_actions == 0) ++terminal;
        else ++conclusive;
    }

    template<class Node>
    void watch(const event::bestmove_selected<Node> &ev)
    {
        const double seconds = std::chrono::duration<double>(ev.duration).count();
        std::ostringstream text;
        text << std::setprecision(17)
             << "mcts_stats elapsed_seconds=" << seconds
             << " iterations=" << ev.iteration
             << " ips=" << (seconds > 0 ? ev.iteration / seconds : 0)
             << " conclusive_rollouts=" << conclusive
             << " inconclusive_rollouts=" << inconclusive
             << " terminal_tree_evaluations=" << terminal;
        if(ev.selected_ceiling)
        {
            std::vector<float> scores;
            for(auto *node = ev.selected_ceiling; node->get_parent(); node = node->get_parent())
            {
                const auto &info = node->get_info();
                if(info.visits) scores.push_back(info.sum_reward / info.visits);
            }
            std::reverse(scores.begin(), scores.end());
            double average = 0;
            for(float score : scores) average += score;
            if(!scores.empty()) average /= scores.size();
            text << "\nmcts_score average=" << average << " detailed=";
            for(std::size_t i = 0; i < scores.size(); ++i)
            {
                if(i) text << ':';
                text << scores[i];
            }
        }
        output = text.str();
    }
    std::string report() const { return output; }
};

using rollout_mcts_engine = basic_mcts_engine<
    uct_tree_policy,
    rollout_default_policy,
    sum_backpropagation,
    most_visited_selection,
    mcts_observer
>;
using zero_mcts_engine = basic_mcts_engine<
    uct_tree_policy,
    zero_default_policy,
    sum_backpropagation,
    most_visited_selection,
    mcts_observer
>;
using weighted_rollout_mcts_engine = basic_mcts_engine<
    uct_tree_policy,
    weighted_rollout_default_policy,
    sum_backpropagation,
    most_visited_selection,
    mcts_observer
>;

class mcts_engine final : public rollout_mcts_engine
{
public:
    mcts_engine(std::unique_ptr<io_handler> io,
                std::optional<std::uint32_t> seed = std::nullopt,
                int max_actions = default_mcts_rollout_max_actions)
        : rollout_mcts_engine(
            rollout_default_policy{random_action_selection{}, rollout_cutoff_evaluation{},
                static_cast<std::size_t>(std::max(0, max_actions)), seed},
            uct_tree_policy{seed}, {}, {}, {}, std::move(io)) {}
};

class zero_engine final : public zero_mcts_engine
{
public:
    zero_engine(std::unique_ptr<io_handler> io,
                std::optional<std::uint32_t> seed = std::nullopt,
                int = default_mcts_rollout_max_actions)
        : zero_mcts_engine({}, uct_tree_policy{seed}, {}, {}, {}, std::move(io)) {}
};

class mcts_weighted_engine final : public weighted_rollout_mcts_engine
{
public:
    mcts_weighted_engine(
        std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed = std::nullopt,
        int max_actions = default_mcts_rollout_max_actions,
        float weight_temperature = default_move_info_temperature)
        : weighted_rollout_mcts_engine(
            weighted_rollout_default_policy{
                weighted_action_selection{
                    default_move_info_weights, weight_temperature},
                rollout_cutoff_evaluation{},
                static_cast<std::size_t>(std::max(0, max_actions)), seed},
            uct_tree_policy{seed}, {}, {}, {}, std::move(io)) {}
};

static_assert(TreePolicy<uct_tree_policy, mcts_observer>);
static_assert(DefaultPolicy<rollout_default_policy, mcts_observer>);
static_assert(DefaultPolicy<weighted_rollout_default_policy, mcts_observer>);
static_assert(DefaultPolicy<zero_default_policy, mcts_observer>);

#endif
