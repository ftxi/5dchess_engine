#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "mcts.h"

namespace
{

class capture_io final : public io_handler
{
public:
    std::vector<std::string> lines;

    std::string read_line() override { return {}; }
    void write_line(const std::string &line) override { lines.push_back(line); }
    bool is_open() override { return false; }
};

std::map<std::string, std::string> parse_stats(const std::string &line)
{
    std::map<std::string, std::string> result;
    std::istringstream input(line);
    std::string token;
    while(input >> token)
    {
        const std::size_t separator = token.find('=');
        if(separator != std::string::npos)
        {
            result.emplace(
                token.substr(0, separator),
                token.substr(separator + 1));
        }
    }
    return result;
}

} /* anonymous namespace */

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

    auto io = std::make_unique<capture_io>();
    capture_io *io_ptr = io.get();
    mcts_engine engine(std::move(io), 17, 0);
    engine.set_position("startpos", "");
    const std::optional<action> best_move
        = engine.find_best_move(1, std::nullopt, {});
    assert(best_move.has_value());

    const auto stats_line = std::find_if(
        io_ptr->lines.begin(),
        io_ptr->lines.end(),
        [](const std::string &line)
        {
            return line.starts_with("info mcts_stats ");
        });
    assert(stats_line != io_ptr->lines.end());
    const auto stats = parse_stats(*stats_line);
    const auto count = [&stats](const std::string &name)
    {
        assert(stats.contains(name));
        return std::stoull(stats.at(name));
    };

    const auto iterations = count("iterations");
    const auto conclusive = count("conclusive_rollouts");
    const auto inconclusive = count("inconclusive_rollouts");
    const auto terminal = count("terminal_tree_evaluations");
    assert(iterations == conclusive + inconclusive + terminal);
    assert(conclusive
           == count("rollout_white_wins")
            + count("rollout_black_wins")
            + count("rollout_stalemates"));
    assert(count("unique_terminal_tree_nodes") <= terminal);
    assert(count("rollout_actions_total") == 0);
    assert(count("rollout_actions_max") == 0);
    assert(count("mcts_nodes_admitted") <= iterations);
    assert(count("root_actions_visited") <= count("root_actions_discovered"));
    assert(count("selected_action_visits") <= iterations);
    assert(count("second_action_visits") <= iterations);
    assert(std::stod(stats.at("rollout_seconds")) >= 0.0);
    assert(std::stod(stats.at("evaluation_seconds")) == 0.0);
    return 0;
}
