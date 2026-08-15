#include "state.h"
#include "pgnparser.h"
#include "rollout.h"
#include "run_rollout.h"
#include <optional>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>

constexpr int MAX_ACTIONS = 200;

constexpr int SIMULATION_NUM = 100;

int run_rollout(int argc, const char *argv[])
{
    const std::string default_pgn = //R"([Board "Standard - Turn Zero"])";
    R"(
[Size "4x4"]
[Timeline "Odd"]
[nbrk/3p*/P*3/KRBN:0:1:w]
[nbrk/3p*/P*R2/K1BN:0:1:b]
[n1rk/2bp*/P*R2/K1BN:0:2:w]
[n1rk/2Np*/P*R2/K1B1:0:2:b]
[n2k/2rp*/P*R2/K1B1:0:3:w]
[n2k/2rp*/P*1R1/K1B1:0:3:b]
[n2k/r2p*/P*1R1/K1B1:0:4:w]
[n2k/r2p*/P*BR1/K3:0:4:b]
[n2k/2rp*/P*BR1/K3:0:5:w]
[n2k/2rp*/P*1R1/K1B1:0:5:b]
[n1rk/3p*/P*1R1/K1B1:0:6:w]
[n1rk/3p*/P*2R/K1B1:0:6:b]
[n2k/3p*/P*2R/K1r1:0:7:w]
[n2k/3p*/P*2R/2r1:0:7:b]
[n2k/3p*/P*2R/3r:0:8:w]
[n2k/3R/P*3/3r:0:8:b]
[n2k/3r/P*3/4:0:9:w]
[n2k/P2r/4/4:0:9:b]
[n2k/r3/4/4:0:10:w]
[n1rk/3p*/P*1R1/KKB1:1:6:b]
[nr1k/3p*/P*1R1/KKB1:1:7:w]
[nr1k/3p*/P*BR1/KK2:1:7:b]
[1r1k/3p*/P*nR1/KK2:1:8:w]
[1r1k/3p*/P*R2/KK2:1:8:b]
[1r1k/4/P*R1p/KK2:1:9:w]
[Qr1k/4/1R1p/KK2:1:9:b]
[Qr1k/4/1R2/KK1q:1:10:w]
)";
    std::string pgn = default_pgn;
    int max_actions = MAX_ACTIONS;
    int simulation_num = SIMULATION_NUM;
    int white_wins = 0;
    int black_wins = 0;
    int no_winner = 0;
    using clock = std::chrono::steady_clock;
    clock::duration total_simulation_duration{};
    bool csv_output = false;
    bool show_help = false;
    bool read_pgn_from_stdin = false;
    auto print_help = [&](std::ostream &out = std::cout) {
        out << "Usage: 5dtools rollout [OPTIONS]\n"
                  << "  -m, --max-actions <n>  limit exploration depth per simulation (default " << MAX_ACTIONS << ")\n"
                  << "  -s, --simulations <n>  number of simulations to run (default " << SIMULATION_NUM << ")\n"
                  << "  -i                     read PGN from stdin until EOF (overrides default position)\n"
                  << "  -csv                   emit CSV with columns simulation,winner,time_ms\n"
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
                std::cerr << "Error: missing argument for " << argv[arg - 1] << "\n";
                print_help(std::cerr);
                return 2;
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
                std::cerr << "Error: invalid number for " << argv[arg - 1] << ": " << argv[arg] << "\n";
                print_help(std::cerr);
                return 2;
            }
            continue;
        }
        if(std::strcmp(argv[arg], "-s") == 0 || std::strcmp(argv[arg], "--simulations") == 0)
        {
            if(++arg >= argc)
            {
                std::cerr << "Error: missing argument for " << argv[arg - 1] << "\n";
                print_help(std::cerr);
                return 2;
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
                std::cerr << "Error: invalid number for " << argv[arg - 1] << ": " << argv[arg] << "\n";
                print_help(std::cerr);
                return 2;
            }
            continue;
        }
        if(std::strcmp(argv[arg], "-i") == 0)
        {
            read_pgn_from_stdin = true;
            continue;
        }
        std::cerr << "Error: unknown option: " << argv[arg] << "\n";
        print_help(std::cerr);
        return 2;
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
            std::cerr << "Error: no PGN data provided on stdin\n";
            return 2;
        }
        pgn = stdin_input;
    }
    std::optional<state> parsed_state;
    try
    {
        parsed_state.emplace(*pgnparser(pgn).parse_game());
    }
    catch(const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    state &s = *parsed_state;
    if(csv_output)
    {
        std::cout << "simulation,winner,time_ms\n";
    }
    std::cout << std::fixed << std::setprecision(2);
    for(int i = 0; i < simulation_num; i++)
    {
        auto start = clock::now();
        const std::optional<bool> winner = rollout(s, max_actions);
        auto duration = clock::now() - start;
        total_simulation_duration += duration;
        double duration_ms = std::chrono::duration<double, std::milli>(duration).count();
        if(csv_output)
        {
            std::cout << (i + 1) << ','
                      << (winner.has_value() ? (*winner ? "black" : "white") : "none") << ','
                      << duration_ms << '\n';
        }
        else
        {
            std::cout << "\rSimulation " << (i + 1) << "/" << simulation_num
                      << " (" << duration_ms
                      << " ms)   ";
            std::cout.flush();
        }
        if(!winner.has_value())
        {
            ++no_winner;
        }
        else if(*winner)
        {
            ++black_wins;
        }
        else
        {
            ++white_wins;
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
    std::cout << "Outcome summary: white=" << percent(white_wins)
              << "%, black=" << percent(black_wins)
              << "%, none=" << percent(no_winner) << "%\n";
    double total_simulation_ms = std::chrono::duration<double, std::milli>(total_simulation_duration).count();
    double avg_simulation_ms = total_simulation_ms / simulation_num;
    std::cout << std::setprecision(2);
    std::cout << "Average simulation time: " << avg_simulation_ms << " ms\n";
    return 0;
}
