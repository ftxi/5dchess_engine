// created by ftxi on 2026-03-09

#include "state.h"
#include "pgnparser.h"
#include "hypercuboid.h"
#include <limits>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

constexpr float INF = std::numeric_limits<float>::infinity();

constexpr int MAX_ACTIONS = 200;

struct simulation_result
{
    float outcome;
    int actions;
    bool limit_reached;
};

simulation_result default_policy(state s, int max_actions)
{
    int num_actions;
    for(num_actions = 0; num_actions < max_actions; num_actions++)
    {
        auto [present, player] = s.get_present();
        auto [w, ss] = HC_info::build_HC(s);
        w.shuffle(ss);
        if(auto mvs = w.iterative_search(ss).first())
        {
            for(full_move fm : *mvs)
            {
                s.apply_move(fm);
            }
            s.submit();
            continue;
        }
        float outcome = player ? -INF : INF;
        // std::cout << "Game ended in depth " << num_actions << ", outcome:" << outcome << ", board:\n";
        // std::cout << s.show_fen() << "\n";
        return {outcome, num_actions, false};
    }
    // std::cout << "Reached depth limit, board:\n";
    // std::cout << s.show_fen() << "\n";
    return {0.0f, num_actions, true};
}

constexpr int SIMULATION_NUM = 100;

int main(int argc, char **argv)
{
    const std::string default_pgn = R"([Board "Standard - Turn Zero"])";
    std::string pgn = default_pgn;
    int max_actions = MAX_ACTIONS;
    int simulation_num = SIMULATION_NUM;
    int inf_count = 0;
    int neg_inf_count = 0;
    int zero_count = 0;
    int limit_reached_count = 0;
    long long actions_total = 0;
    double actions_sq_total = 0.0;
    using clock = std::chrono::steady_clock;
    clock::duration total_simulation_duration{};
    bool csv_output = false;
    bool show_help = false;
    bool read_pgn_from_stdin = false;
    const char *program_name = (argc > 0) ? argv[0] : "rollout";
    auto print_help = [&]() {
        std::cout << "Usage: " << program_name << " [OPTIONS]\n"
                  << "  -m, --max-actions <n>  limit exploration depth per simulation (default " << MAX_ACTIONS << ")\n"
                  << "  -s, --simulations <n>  number of simulations to run (default " << SIMULATION_NUM << ")\n"
                  << "  -i                     read PGN from stdin until EOF (overrides default position)\n"
                  << "  -csv                   emit CSV with columns simulation,outcome,actions,limit_reached,time_ms\n"
                  << "                         (time_ms is the duration of each simulation in milliseconds)\n"
                  << "  -h, --help             display this help text and exit\n";
    };
    for(int arg = 1; arg < argc; arg++)
    {
        if(std::strcmp(argv[arg], "-csv") == 0)
        {
            csv_output = true;
            continue;
        }
        if(std::strcmp(argv[arg], "-h") == 0 || std::strcmp(argv[arg], "--help") == 0)
        {
            show_help = true;
            break;
        }
        if(std::strcmp(argv[arg], "-m") == 0 || std::strcmp(argv[arg], "--max-actions") == 0)
        {
            if(++arg >= argc)
            {
                std::cerr << "Missing argument for " << argv[arg - 1] << "\n";
                return 1;
            }
            try
            {
                size_t consumed = 0;
                int parsed = std::stoi(argv[arg], &consumed);
                if(consumed != std::strlen(argv[arg]) || parsed <= 0)
                {
                    throw std::invalid_argument("non-positive");
                }
                max_actions = parsed;
            }
            catch(const std::exception &)
            {
                std::cerr << "Invalid number for " << argv[arg - 1] << ": " << argv[arg] << "\n";
                return 1;
            }
            continue;
        }
        if(std::strcmp(argv[arg], "-s") == 0 || std::strcmp(argv[arg], "--simulations") == 0)
        {
            if(++arg >= argc)
            {
                std::cerr << "Missing argument for " << argv[arg - 1] << "\n";
                return 1;
            }
            try
            {
                size_t consumed = 0;
                int parsed = std::stoi(argv[arg], &consumed);
                if(consumed != std::strlen(argv[arg]) || parsed <= 0)
                {
                    throw std::invalid_argument("non-positive");
                }
                simulation_num = parsed;
            }
            catch(const std::exception &)
            {
                std::cerr << "Invalid number for " << argv[arg - 1] << ": " << argv[arg] << "\n";
                return 1;
            }
            continue;
        }
        if(std::strcmp(argv[arg], "-i") == 0)
        {
            read_pgn_from_stdin = true;
            continue;
        }
    }
    if(show_help)
    {
        print_help();
        return 0;
    }
    if(read_pgn_from_stdin)
    {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        const std::string stdin_input = buffer.str();
        if(stdin_input.empty())
        {
            std::cerr << "No PGN data provided on stdin\n";
            return 1;
        }
        pgn = stdin_input;
    }
    state s(*pgnparser(pgn).parse_game());
    if(csv_output)
    {
        std::cout << "simulation,outcome,actions,limit_reached,time_ms\n";
    }
    std::cout << std::fixed << std::setprecision(2);
    for(int i = 0; i < simulation_num; i++)
    {
        auto start = clock::now();
        auto result = default_policy(s, max_actions);
        auto duration = clock::now() - start;
        total_simulation_duration += duration;
        double duration_ms = std::chrono::duration<double, std::milli>(duration).count();
        if(csv_output)
        {
            std::cout << (i + 1) << ','
                      << result.outcome << ','
                      << result.actions << ','
                      << (result.limit_reached ? 1 : 0) << ','
                      << duration_ms << '\n';
        }
        else
        {
            std::cout << "\rSimulation " << (i + 1) << "/" << simulation_num
                      << " (" << duration_ms
                      << " ms)   ";
            std::cout.flush();
        }
        actions_total += result.actions;
        actions_sq_total += static_cast<double>(result.actions) * result.actions;
        if(result.outcome == INF)
        {
            ++inf_count;
        }
        else if(result.outcome == -INF)
        {
            ++neg_inf_count;
        }
        else
        {
            ++zero_count;
        }
        if(result.limit_reached)
        {
            ++limit_reached_count;
        }
    }
    if(csv_output)
    {
        return 0;
    }

    std::cout << "\n";
    const auto percent = [total = static_cast<double>(simulation_num)](int count)
    {
        return (count * 100.0) / total;
    };
    std::cout << std::setprecision(1);
    std::cout << "Outcome summary: INF=" << percent(inf_count)
              << "%, -INF=" << percent(neg_inf_count)
              << "%, 0=" << percent(zero_count) << "%\n";
    std::cout << "Limit reached in " << percent(limit_reached_count)
              << "% of simulations\n";
    double total_simulation_ms = std::chrono::duration<double, std::milli>(total_simulation_duration).count();
    double avg_simulation_ms = total_simulation_ms / simulation_num;
    double avg_action_ms = actions_total ? (total_simulation_ms / actions_total) : 0.0;
    double mean_actions = actions_total / static_cast<double>(simulation_num);
    double variance = (actions_sq_total / simulation_num) - (mean_actions * mean_actions);
    if(variance < 0.0)
    {
        variance = 0.0;
    }
    double stddev_actions = std::sqrt(variance);
    std::cout << "Mean terminate actions: "
              << mean_actions << "\n";
    std::cout << "Terminate actions stddev: "
              << stddev_actions << "\n";
    std::cout << std::setprecision(2);
    std::cout << "Average simulation time: " << avg_simulation_ms << " ms\n";
    std::cout << "Average time per action: " << avg_action_ms << " ms\n";
    return 0;
}
