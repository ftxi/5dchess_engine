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
#include "monkey.h"

namespace
{
void print_usage(std::ostream &out)
{
    out << "Usage: 5dchess <mcts|monkey> [--seed|-s] <seed>\n"
        << "       5dchess <mcts|monkey>\n"
        << "  -s, --seed <seed>  optional unsigned 32-bit random seed\n"
        << "  -h, --help         display this help text and exit\n";
}

std::optional<std::uint32_t> parse_seed(int argc, const char *argv[])
{
    if(argc == 2)
    {
        return std::nullopt;
    }
    if(argc != 4 || (std::string(argv[2]) != "--seed" && std::string(argv[2]) != "-s"))
    {
        throw std::invalid_argument("invalid arguments");
    }

    std::size_t consumed = 0;
    const std::string value = argv[3];
    const unsigned long long parsed = std::stoull(value, &consumed);
    if(consumed != value.size() || parsed > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::out_of_range("seed");
    }
    return static_cast<std::uint32_t>(parsed);
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
    if(engine_name != "mcts" && engine_name != "monkey")
    {
        std::cerr << "Unknown engine: " << engine_name << "\n";
        print_usage(std::cerr);
        return 2;
    }

    std::optional<std::uint32_t> seed;
    try
    {
        seed = parse_seed(argc, argv);
    }
    catch(const std::exception &)
    {
        std::cerr << "Error: invalid seed arguments (expected an unsigned 32-bit integer)\n";
        print_usage(std::cerr);
        return 2;
    }

    std::unique_ptr<engine> selected_engine;
    if(engine_name == "mcts")
    {
        selected_engine = std::make_unique<mcts_engine>(
            std::make_unique<stdio_handler>(), seed);
    }
    else if(engine_name == "monkey")
    {
        selected_engine = std::make_unique<monkey_engine>(
            std::make_unique<stdio_handler>(), seed);
    }

    selected_engine->mainloop();
    return 0;
}
