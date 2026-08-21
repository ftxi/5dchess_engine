// Accessing the 5dchess engines

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mcts.h"
#include "linear.h"
#include "monkey.h"
#include "flat_ucb.h"

namespace
{
struct command_line_options
{
    std::optional<std::uint32_t> seed;
    int rollout_max_actions = default_mcts_rollout_max_actions;
};

void print_usage(std::ostream &out)
{
    out << "Usage: 5dchess <mcts|zero|linear|linear-trained|flat-uct|monkey> [options]\n"
        << "  -s, --seed <seed>               optional unsigned 32-bit random seed\n"
        << "  -r, --rollout-max-actions <n>   search rollout action limit (default "
        << default_mcts_rollout_max_actions << ")\n"
        << "  -h, --help                      display this help text and exit\n";
}

unsigned long long parse_unsigned(const std::string &value)
{
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if(consumed != value.size())
    {
        throw std::invalid_argument("trailing characters");
    }
    return parsed;
}

command_line_options parse_options(
    int argc, const char *argv[], const std::string &engine_name)
{
    command_line_options options;
    bool seed_seen = false;
    bool rollout_limit_seen = false;
    for(int i = 2; i < argc; ++i)
    {
        const std::string option = argv[i];
        if(option == "-s" || option == "--seed")
        {
            if(seed_seen || ++i >= argc)
            {
                throw std::invalid_argument("invalid seed option");
            }
            const auto parsed = parse_unsigned(argv[i]);
            if(parsed > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::out_of_range("seed");
            }
            options.seed = static_cast<std::uint32_t>(parsed);
            seed_seen = true;
        }
        else if(option == "-r" || option == "--rollout-max-actions")
        {
            if((engine_name != "mcts" && engine_name != "linear"
                && engine_name != "linear-trained"
                && engine_name != "flat-uct")
               || rollout_limit_seen || ++i >= argc)
            {
                throw std::invalid_argument("invalid rollout limit option");
            }
            const auto parsed = parse_unsigned(argv[i]);
            if(parsed > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
            {
                throw std::out_of_range("rollout limit");
            }
            options.rollout_max_actions = static_cast<int>(parsed);
            rollout_limit_seen = true;
        }
        else
        {
            throw std::invalid_argument("unknown option");
        }
    }
    return options;
}
}

int main(int argc, const char *argv[])
{
    if(argc == 2 && (std::string_view(argv[1]) == "-h"
                     || std::string_view(argv[1]) == "--help"))
    {
        print_usage(std::cout);
        return 0;
    }
    if(argc < 2)
    {
        std::cerr << "Error: missing engine name\n";
        print_usage(std::cerr);
        return 2;
    }

    const std::string engine_name = argv[1];
    if(engine_name != "mcts" && engine_name != "zero" && engine_name != "linear"
       && engine_name != "linear-trained"
       && engine_name != "flat-uct" && engine_name != "monkey")
    {
        std::cerr << "Unknown engine: " << engine_name << "\n";
        print_usage(std::cerr);
        return 2;
    }

    command_line_options options;
    try
    {
        options = parse_options(argc, argv, engine_name);
    }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid options\n";
        print_usage(std::cerr);
        return 2;
    }

    std::unique_ptr<engine> selected_engine;
    if(engine_name == "mcts")
    {
        selected_engine = std::make_unique<mcts_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions);
    }
    else if(engine_name == "zero")
    {
        selected_engine = std::make_unique<zero_engine>(
            std::make_unique<stdio_handler>(), options.seed);
    }
    else if(engine_name == "linear" || engine_name == "linear-trained")
    {
        const auto weights = engine_name == "linear"
            ? linear_engine::default_weights()
            : linear_engine::trained_weights();
        selected_engine = std::make_unique<linear_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions, weights);
    }
    else if(engine_name == "flat-uct")
    {
        selected_engine = std::make_unique<flat_ucb_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions);
    }
    else if(engine_name == "monkey")
    {
        selected_engine = std::make_unique<monkey_engine>(
            std::make_unique<stdio_handler>(), options.seed);
    }

    selected_engine->mainloop();
    return 0;
}
