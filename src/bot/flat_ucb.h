#ifndef BOT_FLAT_UCB_H
#define BOT_FLAT_UCB_H

#include <cstddef>
#include <utility>
#include <vector>
#include <random>
#include "state.h"
#include "hypercuboid.h"

// Default exploration constant (sqrt(2)) for the UCT selection policy
constexpr double exploration_constant = 1.4142135623730951;

// UCT score for a node using cumulative reward and visit counts.
/* returns average_reward + exploration_term
   where average_reward = sum_reward / visits
       exploration_term = exploration_constant * sqrt(log(parent_visits) / visits)
*/ 
double uct(double sum_reward, std::size_t visits, std::size_t parent_visits);

inline double default_policy(state s, int max_actions)
{
    int num_actions;
    for(num_actions = 0; num_actions < max_actions; num_actions++)
    {
        auto [w, ss] = HC_info::build_HC(s);
        w.shuffle(ss);
        if(auto mvs = w.search(ss).first())
        {
            for(const full_move& fm : *mvs)
            {
                s.apply_move(fm);
            }
            s.submit();
            continue;
        }
        auto [t, c] = s.get_present();
        if(s.phantom().find_checks(!c).first().has_value())
        {
            return c ? 1.0 : -1.0;
        }
        else
        {
            return 0.0;
        }
    }
    // std::cout << "Reached depth limit, board:\n";
    // std::cout << s.show_fen() << "\n";
    return 0.0;
}

class flat_ucb_bot
{
    state initial_state;
    const std::size_t num_nodes;
    const std::vector<moveseq> actions;
    std::vector<state> node_states;
    std::vector<double> sum_rewards;
    std::vector<std::size_t> visits;
    size_t total_visits;

    flat_ucb_bot(state s, std::vector<moveseq> actions_list)
    :   initial_state{s},
        num_nodes{actions_list.size()},
        actions{std::move(actions_list)},
        node_states(),
        sum_rewards(num_nodes, 0.0),
        visits(num_nodes, 0),
        total_visits(0)
    {
        node_states.reserve(num_nodes);
        for(const auto& mvs : actions)
        {
            state t = s;
            for(const full_move& fm : mvs)
            {
                t.apply_move(fm);
            }
            t.submit();
            node_states.push_back(std::move(t));
        }
    }
public:
    static flat_ucb_bot make_bot(state s)
    {
        auto [w, ss] = HC_info::build_HC(s);
        std::vector<moveseq> actions;
        for(const auto& mvs : w.search(ss))
        {
            actions.push_back(mvs);
        }
        return flat_ucb_bot(s, actions);
    }
    int best_child() const
    {
        int best_index = 0;
        double best_score = -1.0;
        for (std::size_t i = 0; i < sum_rewards.size(); ++i)
        {
            double score = uct(sum_rewards[i], visits[i], total_visits);
            if (score > best_score)
            {
                best_score = score;
                best_index = static_cast<int>(i);
            }
        }
        return best_index;
    }
    void backup(std::size_t child, double reward)
    {
        sum_rewards[child] += reward;
        visits[child] += 1;
        total_visits += 1;
    }
    double run_iteration(int max_actions = 200)
    {
        int child = best_child();
        double reward = default_policy(node_states[child], max_actions);
        backup(child, reward);
        return reward;
    }

    void search(std::size_t iterations)
    {
        for (std::size_t i = 0; i < iterations; ++i)
        {
            run_iteration();
        }
    }

    size_t most_visited_child() const
    {
        std::size_t best_index = 0;
        std::size_t best_visits = 0;
        for (std::size_t i = 0; i < visits.size(); ++i)
        {
            if (visits[i] > best_visits)
            {
                best_visits = visits[i];
                best_index = i;
            }
        }
        return best_index;
    }

    const std::vector<double>& get_sum_rewards() const { return sum_rewards; }
    const std::vector<std::size_t>& get_visits() const { return visits; }
    const moveseq& get_action(size_t i) const { return actions[i]; }
};

#endif // BOT_FLAT_UCB_H
