#include "action.h"
#include "bot/flat_ucb.h"
#include "state.h"
#include "pgnparser.h"
#include <chrono>
#include <iostream>

int main()
{
    std::string pgn = R"(
[Board "Very Small - Open"]
1. Rxb4 / Rxb4 
2. Ba3 / Rc4 
3. Kb1 / Nc3+ 
4. Nxc3 / Kxc3 
5w. Kc1 / Ra4 
    )";
    state s(*pgnparser(pgn).parse_game());
    flat_ucb_bot bot = flat_ucb_bot::make_bot(s);
    
    constexpr auto time_limit = std::chrono::seconds(5);
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + time_limit;
    int iterations = 0;
    while(clock::now() < deadline)
    {
        iterations++;
        auto start = clock::now();
        bot.run_iteration();
        auto duration = clock::now() - start;
        double duration_ms = std::chrono::duration<double, std::milli>(duration).count();
        std::cout << "\rIteration " << iterations
                  << " (" << duration_ms << " ms)   ";
        std::cout.flush();
    }
    std::cout << "\n";
    
    std::cout << "Estimated scores after " << iterations << " iterations (~5s):\n";
    const auto &sum_rewards = bot.get_sum_rewards();
    const auto &visits = bot.get_visits();

    for(std::size_t i = 0; i < sum_rewards.size(); i++)
    {
        action act = action::from_moveseq(bot.get_action(i), s);
        std::cout << "Action " << i << ": " << s.pretty_action<state::SHOW_CAPTURE | state::SHOW_MATE | state::SHOW_SHORT>(act) << "\n";
        double estimated_prob = visits[i] ? sum_rewards[i] / static_cast<double>(visits[i]) : 0.0;
        std::cout << "Estimated Score = " << estimated_prob << " (Visits: " << visits[i] << ")\n";
    }
    int best_index = bot.most_visited_child();
    std::cout << "Best action index: " << best_index << "\n";
    action act = action::from_moveseq(bot.get_action(best_index), s);
    std::cout << s.pretty_action<state::SHOW_CAPTURE | state::SHOW_MATE | state::SHOW_SHORT>(act) << "\n";
    return 0;
}