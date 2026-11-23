// Accessing the 5dchess engines

#include <charconv>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mcts_engines.h"
#include "solid_amaf.h"
#include "linear.h"
#include "monkey.h"
#include "flat_ucb.h"

namespace
{
struct command_line_options
{
    std::optional<std::uint32_t> seed;
    int rollout_max_actions = default_mcts_rollout_max_actions;
    float weight_temperature = default_rollout_temperature;
    double progressive_widening_constant
        = default_progressive_widening_constant;
    double progressive_widening_alpha = default_progressive_widening_alpha;
    std::size_t fine_prefix_budget = default_fine_prefix_budget;
};

bool is_weighted_engine(std::string_view name)
{
    return name == "mcts-weighted" || name == "linear-weighted"
        || name == "flat-ucb-weighted";
}

std::optional<solid_amaf_blend> amaf_blend_for(std::string_view name)
{
    if(name == "solid-amaf") return solid_amaf_blend::pooled;
    if(name == "solid-amaf1") return solid_amaf_blend::half_pooled;
    if(name == "solid-amaf2") return solid_amaf_blend::half_outside;
    return std::nullopt;
}

bool is_mvvlva_engine(std::string_view name)
{
    return name == "mvvlva" || name == "mvvlva-exposure"
        || name == "mvvlva-pruned" || name == "mvvlva-solid" || name == "solid"
        || amaf_blend_for(name).has_value();
}

bool supports_rollout_limit(std::string_view name)
{
    return name == "mcts" || name == "linear" || name == "linear-trained"
        || name == "flat-ucb" || is_weighted_engine(name);
}

void print_usage(std::ostream &out)
{
    out << "Usage: 5dchess <engine> [options]\n"
        << "\nEngines:\n"
        << "  mcts               Monte Carlo tree search with randomized rollouts\n"
        << "  mcts-weighted      MCTS with move-weighted rollouts\n"
        << "  zero               MCTS with no rollout and zero cutoff evaluation\n"
        << "  zero-capture       zero with capture-biased tree expansion\n"
        << "  zero-capture-check-pw  capture/check ordering with progressive widening\n"
        << "  mvvlva             MVV-LVA ordering with progressive widening\n"
        << "  mvvlva-exposure    MVV-LVA with local exposure and promotion scoring\n"
        << "  mvvlva-pruned      exposure with optional timeline move pruning\n"
        << "  mvvlva-solid       legacy optional-move pruning experiment\n"
        << "  solid              score-weighted UCT with optional-move penalties and check pre-pruning\n"
        << "  solid-amaf         solid with action-local AMAF for semimove revisits\n"
        << "  solid-amaf1        half-strength AMAF using the pooled mean\n"
        << "  solid-amaf2        half-strength AMAF weighted by visits from other prefixes\n"
        << "  linear             MCTS with random rollouts and linear cutoff evaluation\n"
        << "  linear-trained     linear with a frozen experimental cutoff profile\n"
        << "  linear-weighted    MCTS with weighted rollouts and linear cutoff evaluation\n"
        << "  flat-ucb           flat upper-confidence-bound search with random rollouts\n"
        << "  flat-ucb-weighted  flat UCB search with weighted rollouts\n"
        << "  monkey             uniformly select one legal action\n"
        << "\nOptions:\n"
        << "  -s, --seed <seed>               optional unsigned 32-bit random seed\n"
        << "  -r, --rollout-max-actions <n>   search rollout action limit (default "
        << default_mcts_rollout_max_actions << ")\n"
        << "  -wt, --weight-temperature <n>   weighted rollout temperature (default "
        << default_rollout_temperature << ")\n"
        << "  --pw-constant <n>               widening constant for PW engines"
           " (default 2)\n"
        << "  --pw-alpha <n>                  widening exponent in (0, 1] for"
           " *-pw engines (default 0.5)\n"
        << "  --fine-prefix-budget <n>        work units per ranked group for zero and PW engines"
           " (default " << default_fine_prefix_budget << ", 0 for strict ordering)\n"
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

float parse_positive_float(const std::string &value)
{
    std::size_t consumed = 0;
    const float parsed = std::stof(value, &consumed);
    if(consumed != value.size() || !(parsed > 0.0f) || !std::isfinite(parsed))
    {
        throw std::invalid_argument("not a positive finite number");
    }
    return parsed;
}

command_line_options parse_options(
    int argc, const char *argv[], const std::string &engine_name)
{
    command_line_options options;
    bool seed_seen = false;
    bool rollout_limit_seen = false;
    bool weight_temperature_seen = false;
    bool pw_constant_seen = false;
    bool pw_alpha_seen = false;
    bool fine_prefix_budget_seen = false;
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
            if(!supports_rollout_limit(engine_name) || rollout_limit_seen
               || ++i >= argc)
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
        else if(option == "-wt" || option == "--weight-temperature")
        {
            if(!is_weighted_engine(engine_name) || weight_temperature_seen
               || ++i >= argc)
            {
                throw std::invalid_argument("invalid weight temperature option");
            }
            options.weight_temperature = parse_positive_float(argv[i]);
            weight_temperature_seen = true;
        }
        else if(option == "--pw-constant")
        {
            if((engine_name != "zero-capture-check-pw" && !is_mvvlva_engine(engine_name)) || pw_constant_seen
               || ++i >= argc)
            {
                throw std::invalid_argument("invalid widening constant option");
            }
            options.progressive_widening_constant
                = parse_positive_float(argv[i]);
            pw_constant_seen = true;
        }
        else if(option == "--pw-alpha")
        {
            if((engine_name != "zero-capture-check-pw" && !is_mvvlva_engine(engine_name)) || pw_alpha_seen
               || ++i >= argc)
            {
                throw std::invalid_argument("invalid widening alpha option");
            }
            options.progressive_widening_alpha = parse_positive_float(argv[i]);
            if(options.progressive_widening_alpha > 1.0)
                throw std::invalid_argument("widening alpha exceeds one");
            pw_alpha_seen = true;
        }
        else if(option == "--fine-prefix-budget")
        {
            if((engine_name != "zero" && engine_name != "zero-capture-check-pw"
                && !is_mvvlva_engine(engine_name))
               || fine_prefix_budget_seen || ++i >= argc)
                throw std::invalid_argument("invalid fine prefix budget option");

            const std::string_view value = argv[i];
            const auto end = value.data() + value.size();
            const auto [parsed, error] = std::from_chars(value.data(), end, options.fine_prefix_budget);
            if(error != std::errc{} || parsed != end)
                throw std::invalid_argument("invalid fine prefix budget");
            fine_prefix_budget_seen = true;
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
    if(engine_name != "mcts" && engine_name != "mcts-weighted"
       && engine_name != "zero" && engine_name != "zero-capture"
       && engine_name != "zero-capture-check-pw" && !is_mvvlva_engine(engine_name)
       && engine_name != "linear"
       && engine_name != "linear-trained" && engine_name != "linear-weighted"
       && engine_name != "flat-ucb" && engine_name != "flat-ucb-weighted"
       && engine_name != "monkey")
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
    else if(engine_name == "mcts-weighted")
    {
        selected_engine = std::make_unique<mcts_weighted_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions, options.weight_temperature);
    }
    else if(engine_name == "zero")
    {
        selected_engine = std::make_unique<zero_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions, options.fine_prefix_budget);
    }
    else if(engine_name == "zero-capture")
    {
        selected_engine = std::make_unique<zero_capture_engine>(
            std::make_unique<stdio_handler>(), options.seed);
    }
    else if(engine_name == "zero-capture-check-pw")
    {
        selected_engine = std::make_unique<zero_capture_check_pw_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.progressive_widening_constant, options.progressive_widening_alpha,
            options.fine_prefix_budget);
    }
    else if(is_mvvlva_engine(engine_name))
    {
        if(engine_name == "mvvlva")
            selected_engine = std::make_unique<mvvlva_engine>(
                std::make_unique<stdio_handler>(), options.seed,
                options.progressive_widening_constant, options.progressive_widening_alpha,
                HC_build_options{}, options.fine_prefix_budget);
        else if(const auto blend = amaf_blend_for(engine_name))
            selected_engine = std::make_unique<solid_amaf_engine>(
                std::make_unique<stdio_handler>(), options.seed,
                options.progressive_widening_constant, options.progressive_widening_alpha,
                options.fine_prefix_budget, *blend);
        else if(engine_name == "solid")
            selected_engine = std::make_unique<solid_engine>(
                std::make_unique<stdio_handler>(), options.seed,
                options.progressive_widening_constant, options.progressive_widening_alpha,
                options.fine_prefix_budget);
        else
            selected_engine = std::make_unique<mvvlva_exposure_engine>(
                std::make_unique<stdio_handler>(), options.seed,
                options.progressive_widening_constant, options.progressive_widening_alpha,
                HC_build_options{
                    .remove_check = engine_name == "mvvlva-solid",
                    .prune_optional_moves = engine_name == "mvvlva-pruned" || engine_name == "mvvlva-solid"},
                capture_ordering::solid,
                options.fine_prefix_budget);
    }
    else if(engine_name == "linear" || engine_name == "linear-trained"
            || engine_name == "linear-weighted")
    {
        const auto weights = engine_name == "linear-trained"
            ? linear_cutoff_evaluation::trained_weights()
            : linear_cutoff_evaluation::default_weights();
        if(engine_name == "linear-weighted")
        {
            selected_engine = std::make_unique<linear_weighted_engine>(
                std::make_unique<stdio_handler>(), options.seed,
                options.rollout_max_actions, weights,
                options.weight_temperature);
        }
        else
        {
            selected_engine = std::make_unique<linear_engine>(
                std::make_unique<stdio_handler>(), options.seed,
                options.rollout_max_actions, weights);
        }
    }
    else if(engine_name == "flat-ucb")
    {
        selected_engine = std::make_unique<flat_ucb_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions);
    }
    else if(engine_name == "flat-ucb-weighted")
    {
        selected_engine = std::make_unique<flat_ucb_weighted_engine>(
            std::make_unique<stdio_handler>(), options.seed,
            options.rollout_max_actions, options.weight_temperature);
    }
    else if(engine_name == "monkey")
    {
        selected_engine = std::make_unique<monkey_engine>(
            std::make_unique<stdio_handler>(), options.seed);
    }

    selected_engine->mainloop();
    return 0;
}
