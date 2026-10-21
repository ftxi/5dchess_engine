#include "finetree.h"
#include "mcts_engines.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pgnparser.h"
#include "ordering.h"
#include "replay_log.h"
#include "engine_cli.h"
#include "search_trace.h"

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

void print_help(std::ostream &out)
{
    out << "Usage: 5dtools replay-log <log-or-snapshot.5dpgn> [seed] [--movetime ms]\n"
        << "       [--engine <name> [engine-options...]]\n"
        << "  Default: mcts, 1000 ms. Engine options use the 5dchess syntax.\n"
        << "  --engine and its options must come last. Snapshot inputs also time\n"
        << "  HC construction and a complete zero-evaluator legality search.\n";
}

} /* anonymous namespace */

int replay_log(int argc, const char *argv[])
{
    if(argc == 2 && (std::string_view(argv[1]) == "-h"
                     || std::string_view(argv[1]) == "--help"))
    {
        print_help(std::cout);
        return 0;
    }
    if(argc < 2)
    {
        print_help(std::cerr);
        return 2;
    }

    try
    {
        int movetime = 1000;
        std::optional<std::string> legacy_seed;
        std::vector<std::string> engine_args{"5dchess", "mcts"};
        for(int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if(arg == "--movetime" && i + 1 < argc)
            {
                std::size_t consumed;
                const std::string value = argv[++i];
                movetime = std::stoi(value, &consumed);
                if(consumed != value.size() || movetime < 0)
                    throw std::invalid_argument("invalid movetime");
            }
            else if(arg == "--engine" && i + 1 < argc)
            {
                engine_args.resize(1);
                while(++i < argc) engine_args.emplace_back(argv[i]);
                break;
            }
            else if(i == 2 && !arg.starts_with('-')) legacy_seed = arg;
            else throw std::invalid_argument("unknown replay option: " + arg);
        }
        if(legacy_seed)
        {
            engine_args.emplace_back("--seed");
            engine_args.push_back(*legacy_seed);
        }
        std::vector<const char *> engine_argv;
        for(const auto &arg : engine_args) engine_argv.push_back(arg.c_str());
        auto selected = make_engine(static_cast<int>(engine_argv.size()), engine_argv.data(),
                                    std::make_unique<sink_io>());
        std::ifstream input(argv[1]);
        if(!input)
        {
            throw std::runtime_error("cannot open log: " + std::string(argv[1]));
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        std::string text = contents.str();
        constexpr std::string_view position_marker = "> position ";
        const std::size_t position_pos = text.rfind(position_marker);
        const bool snapshot = position_pos == std::string::npos;
        std::string pgn;
        auto started = clock_type::now();
        if(snapshot)
        {
            // Set up directly through the native state API to preserve promotion
            // rules as well as timeline parity, history, and unmoved flags.
            pgn = text;
            selected->set_position(state::from_pgn(pgn));
        }
        else
        {
            const auto command_end = text.find(" | > go ", position_pos);
            if(command_end == std::string::npos)
                throw std::runtime_error("log has no go after final position");
            const std::string position_command = text.substr(
                position_pos + position_marker.size(),
                command_end - position_pos - position_marker.size());
            const auto moves_pos = position_command.find(" moves ");
            selected->set_position(position_command.substr(0, moves_pos),
                moves_pos == std::string::npos ? "" : position_command.substr(moves_pos + 7));
        }
        if(!selected->get_current_state())
            throw std::runtime_error("replayed position was rejected");
        std::cout << "set_position " << seconds_since(started) << " s\n" << std::flush;
        started = clock_type::now();
        auto best = selected->find_best_move(std::nullopt, movetime, {});
        std::cout << selected->search_configuration() << "\nmovetime " << movetime << ' '
                  << seconds_since(started) << " s, result=" << (best ? "move" : "none")
                  << "\n" << std::flush;

        constexpr std::string_view marker = "Partial game PGN:\n";
        const std::size_t marker_pos = text.rfind(marker);
        if(!snapshot)
        {
            if(marker_pos == std::string::npos) return 0;
            pgn = text.substr(marker_pos + marker.size());
        }

        started = clock_type::now();
        state s = state::from_pgn(pgn);
        std::cout << "parse " << seconds_since(started) << " s\n" << std::flush;

        started = clock_type::now();
        auto [hc_info, search_space] = HC_info::build_HC(s);
        std::cout << "build_HC " << seconds_since(started)
                  << " s, dimensions=" << hc_info.dimension()
                  << ", initial_hcs=" << search_space.size() << "\n"
                  << std::flush;

        started = clock_type::now();
        auto evaluated = evaluate_zero_position(s, {});
        std::cout << "zero evaluation (uncapped) " << seconds_since(started)
                  << " s, result=" << (evaluated ? "complete" : "interrupted") << '\n' << std::flush;

        started = clock_type::now();
        auto root = fine_node<>::make_root(s);
        std::cout << "make_root " << seconds_since(started) << " s\n"
                  << std::flush;

        started = clock_type::now();
        auto first = root->search(natural_HC_ordering{}).first();
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
        return 0;
    }
    catch(const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
