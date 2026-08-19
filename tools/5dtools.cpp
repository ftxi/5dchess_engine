#include <array>
#include <exception>
#include <iostream>
#include <string_view>

#include "position_tools.h"
#include "replay_log.h"
#include "run_perftest.h"
#include "run_rollout.h"
#include "train_linear.h"

namespace
{
using command_handler = int (*)(int, const char *[]);

struct command
{
    std::string_view name;
    std::string_view usage;
    std::string_view description;
    command_handler run;
};

// Add new 5dtools commands here. The implementation can live in any .cpp file
// under tools/; CMake discovers it automatically.
constexpr std::array commands{
    command{"print", "", "print the final state of a 5DPGN game", run_print},
    command{"count", "[policy] [max]", "count available actions", run_count},
    command{"all", "[policy] [max]", "print available actions", run_all},
    command{"checkmate", "[policy]", "detect checkmate or stalemate", run_checkmate},
    command{"diff", "", "compare balanced and naive searches", run_diff},
    command{"perftest", "[policy]", "check every position in a 5DPGN game", run_perftest},
    command{"rollout", "[options]", "run random rollout simulations", run_rollout},
    command{"train-linear", "[options] <training-data> <output-weights>",
            "fit and save linear evaluation weights", run_train_linear},
    command{"replay-log", "<log> [seed]", "replay and time a protocol failure log", replay_log},
};

void print_help()
{
    std::cout << "usage: 5dtools <command> [options]\n\nCommands:\n";
    for(const command &entry : commands)
    {
        std::cout << "  " << entry.name;
        if(!entry.usage.empty())
        {
            std::cout << ' ' << entry.usage;
        }
        std::cout << "\n      " << entry.description << '\n';
    }
    std::cout
        << "\nRun '5dtools <command> --help' for detailed command usage.\n"
        << "\nSearch policies: balanced, naive, stable, iterative, mixed\n"
        << "The print, count, all, checkmate, diff, and perftest commands read 5DPGN from stdin.\n";
}
}

int main(int argc, const char *argv[])
{
    if(argc <= 1 || std::string_view(argv[1]) == "help"
       || std::string_view(argv[1]) == "-h"
       || std::string_view(argv[1]) == "--help")
    {
        print_help();
        return 0;
    }

    const std::string_view requested = argv[1];
    if(requested == "version" || requested == "-v" || requested == "--version")
    {
#ifdef PROJECT_VERSION_STRING
        std::cout << "5d Chess Engine version " << PROJECT_VERSION_STRING << '\n';
#else
        std::cout << "5d Chess Engine version unknown\n";
#endif
        return 0;
    }

    for(const command &entry : commands)
    {
        if(entry.name == requested)
        {
            try
            {
                return entry.run(argc - 1, argv + 1);
            }
            catch(const std::exception &error)
            {
                std::cerr << "Error: " << error.what() << '\n';
                return 1;
            }
        }
    }

    std::cerr << "Unknown command: " << requested << "\n\n";
    print_help();
    return 2;
}
