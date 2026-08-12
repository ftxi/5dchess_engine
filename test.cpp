#include "finetree.h"
#include "mcts.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "pgnparser.h"
#include "ordering.h"

namespace
{
using clock_type = std::chrono::steady_clock;

class sink_io final : public io_handler
{
public:
    std::string read_line() override { return {}; }
    void write_line(const std::string &) override {}
    bool is_open() override { return false; }
};

double seconds_since(clock_type::time_point started)
{
    return std::chrono::duration<double>(clock_type::now() - started).count();
}
}

int main(int argc, char **argv)
{
    if(argc < 2 || argc > 3)
    {
        std::cerr << "usage: " << argv[0]
                  << " protocol-failure-log [rollout-seed]\n";
        return 2;
    }

    const std::optional<std::uint32_t> rollout_seed = argc == 3
        ? std::optional<std::uint32_t>{
            static_cast<std::uint32_t>(std::stoul(argv[2]))}
        : std::nullopt;

    std::ifstream input(argv[1]);
    if(!input)
    {
        throw std::runtime_error("cannot open log");
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    std::string text = contents.str();
    constexpr std::string_view position_marker = "> position ";
    const std::size_t position_pos = text.rfind(position_marker);
    if(position_pos == std::string::npos)
    {
        throw std::runtime_error("log has no UCI position");
    }
    const std::size_t go_pos = text.find(" | > go movetime", position_pos);
    if(go_pos == std::string::npos)
    {
        throw std::runtime_error("log has no go after final position");
    }
    const std::string position_command = text.substr(
        position_pos + position_marker.size(),
        go_pos - position_pos - position_marker.size());
    const std::size_t moves_pos = position_command.find(" moves ");
    if(moves_pos == std::string::npos)
    {
        throw std::runtime_error("final UCI position has no moves");
    }
    const std::string initial_position = position_command.substr(0, moves_pos);
    const std::string moves = position_command.substr(moves_pos + 7);

    mcts_engine mcts(std::make_unique<sink_io>(), rollout_seed);
    std::cout << "rollout seed "
              << (rollout_seed.has_value()
                    ? std::to_string(*rollout_seed)
                    : "random")
              << '\n';
    auto started = clock_type::now();
    mcts.set_position(initial_position, moves);
    std::cout << "UCI set_position " << seconds_since(started) << " s\n"
              << std::flush;

    started = clock_type::now();
    auto best = mcts.find_best_move(std::nullopt, 1000, {});
    std::cout << "MCTS movetime 1000 " << seconds_since(started)
              << " s, result=" << (best ? "move" : "none") << "\n"
              << std::flush;

    constexpr std::string_view marker = "Partial game PGN:\n";
    const std::size_t marker_pos = text.rfind(marker);
    if(marker_pos == std::string::npos)
    {
        throw std::runtime_error("log has no partial PGN");
    }
    const std::string pgn = text.substr(marker_pos + marker.size());

    started = clock_type::now();
    state s(*pgnparser(pgn).parse_game());
    std::cout << "parse " << seconds_since(started) << " s\n" << std::flush;

    started = clock_type::now();
    auto [hc_info, search_space] = HC_info::build_HC(s);
    std::cout << "build_HC " << seconds_since(started)
              << " s, dimensions=" << hc_info.dimension
              << ", initial_hcs=" << search_space.size() << "\n"
              << std::flush;

    started = clock_type::now();
    auto root = fine_node<>::make_root(s);
    std::cout << "make_root " << seconds_since(started) << " s\n"
              << std::flush;

    started = clock_type::now();
    auto first = root->search().first();
    std::cout << "root search.first " << seconds_since(started) << " s, result=";
    if(first)
    {
        std::cout << *first;
    }
    else
    {
        std::cout << "none";
    }
    std::cout << '\n';
}
