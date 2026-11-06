// Accessing the 5dchess engines

#include <cstdint>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mcts_engines.h"
#include "linear.h"
#include "neural.h"
#include "amaf.h"
#include "monkey.h"
#include "flat_ucb.h"
#include "engine_cli.h"

namespace
{
struct command_line_options
{
    std::optional<std::uint32_t> seed;
    int rollout_max_actions = default_mcts_rollout_max_actions;
    float weight_temperature = default_rollout_temperature;
    std::string evaluation_features = "all";
    std::string model_path;
    double progressive_widening_constant
        = default_progressive_widening_constant;
    double progressive_widening_alpha = default_progressive_widening_alpha;
    double pn_constant = default_pn_mcts_constant;
    amaf_options amaf;
    float value_scale = 1.0f;
    bool cache_evaluations = false;
};

bool is_weighted_engine(std::string_view name)
{
    return name == "mcts-weighted" || name == "linear-weighted"
        || name == "flat-ucb-weighted";
}

bool supports_rollout_limit(std::string_view name)
{
    return name == "mcts" || name == "linear" || name == "linear-trained"
        || name == "flat-ucb" || is_weighted_engine(name);
}

bool supports_progressive_widening(std::string_view name)
{
    return name == "zero-capture-check-pw" || name == "mvvlva"
        || name == "mvvlva-exposure" || name == "linear-pw"
        || name == "linear-safety-pw" || name == "linear-trained-pw"
        || name == "neural-pw" || name == "mvvlva-exposure-pn"
        || name == "amaf-pw";
}

bool supports_evaluation_features(std::string_view name)
{
    return name == "linear-pw" || name == "linear-safety-pw"
        || name == "linear-trained-pw";
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
        << "  mvvlva             MVV-LVA/check ordering with progressive widening\n"
        << "  mvvlva-exposure    MVV-LVA with local capture-exposure penalties\n"
        << "  mvvlva-exposure-pn MVV-LVA exposure/PW with PN-MCTS tree selection\n"
        << "  linear             MCTS with random rollouts and linear cutoff evaluation\n"
        << "  linear-trained     linear with a frozen experimental cutoff profile\n"
        << "  linear-weighted    MCTS with weighted rollouts and linear cutoff evaluation\n"
        << "  linear-pw          MVV-LVA exposure/PW with 64-feature leaf evaluation\n"
        << "  linear-safety-pw   linear-pw with six additional royal-safety features\n"
        << "  linear-trained-pw  linear-pw with the frozen experimental weights\n"
        << "  neural-pw          MVV-LVA exposure/PW with a two-hidden-layer model\n"
        << "  amaf-pw            action-local AMAF/PW, optional neural leaf model\n"
        << "  flat-ucb           flat upper-confidence-bound search with random rollouts\n"
        << "  flat-ucb-weighted  flat UCB search with weighted rollouts\n"
        << "  monkey             uniformly select one legal action\n"
        << "\nOptions:\n"
        << "  -s, --seed <seed>               optional unsigned 32-bit random seed\n"
        << "  -r, --rollout-max-actions <n>   search rollout action limit (default "
        << default_mcts_rollout_max_actions << ")\n"
        << "  -wt, --weight-temperature <n>   weighted rollout temperature (default "
        << default_rollout_temperature << ")\n"
        << "  --pw-constant <n>               widening constant for capture/check PW"
           " (default 2)\n"
        << "  --pw-alpha <n>                  widening exponent in (0, 1] for"
           " progressive widening engines (default 0.5)\n"
        << "  --evaluation-features <subset> linear PW leaf features (default all):\n"
        << "      all, none, mandatory-material, material, material-timeline,\n"
        << "      material-volume, material-safety, safety, timeline, volume, timeline-volume\n"
        << "  --model <path>                  required portable model for neural-pw\n"
        << "                                  optional for amaf-pw (otherwise zero leaves)\n"
        << "  --amaf-strength <n>             AMAF mixing strength in [0,1] (default 1)\n"
        << "  --amaf-horizon <n>              AMAF decay visit scale (default 32)\n"
        << "  --amaf-decay <name>             inverse, sqrt, exponential (default inverse)\n"
        << "  --amaf-credit <name>            blend or centered (default blend)\n"
        << "  --amaf-application <name>       selection, admission, children, both\n"
        << "                                  (default selection)\n"
        << "  --amaf-evidence <name>          visits (default) or unique-leaves\n"
        << "  --value-scale <n>               amaf-pw nonterminal neural scale in [0,1]\n"
        << "  --neural-usage <name>           amaf-pw: leaf or amaf-prior (default leaf)\n"
        << "  --cache-evaluations            amaf-pw: reuse immutable node evaluations\n"
        << "  --pn-constant <n>               nonnegative PN rank bias for"
           " mvvlva-exposure-pn (default 1)\n"
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
    bool evaluation_features_seen = false;
    bool pn_constant_seen = false;
    bool amaf_strength_seen = false;
    bool amaf_horizon_seen = false;
    bool amaf_decay_seen = false;
    bool amaf_credit_seen = false;
    bool amaf_application_seen = false;
    bool amaf_evidence_seen = false;
    bool value_scale_seen = false;
    bool neural_usage_seen = false;
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
        else if(option == "--pn-constant")
        {
            if(engine_name != "mvvlva-exposure-pn" || pn_constant_seen
               || ++i >= argc)
                throw std::invalid_argument("invalid PN constant option");
            std::size_t consumed = 0;
            const std::string value = argv[i];
            options.pn_constant = std::stod(value, &consumed);
            if(consumed != value.size() || !(options.pn_constant >= 0.0)
               || !std::isfinite(options.pn_constant))
                throw std::invalid_argument("PN constant must be nonnegative and finite");
            pn_constant_seen = true;
        }
        else if(option == "--cache-evaluations")
        {
            if(engine_name != "amaf-pw" || options.cache_evaluations)
                throw std::invalid_argument("invalid evaluation cache option");
            options.cache_evaluations = true;
        }
        else if(option == "--amaf-strength" || option == "--amaf-horizon"
                || option == "--value-scale")
        {
            bool &seen = option == "--amaf-strength" ? amaf_strength_seen
                : option == "--amaf-horizon" ? amaf_horizon_seen : value_scale_seen;
            if(engine_name != "amaf-pw" || seen || ++i >= argc)
                throw std::invalid_argument("invalid AMAF numeric option");
            std::size_t consumed = 0;
            const std::string value = argv[i];
            const double number = std::stod(value, &consumed);
            if(consumed != value.size() || !(number >= 0.0) || !std::isfinite(number)
                || (option != "--amaf-horizon" && number > 1.0))
                throw std::invalid_argument("invalid AMAF strength, horizon or value scale");
            if(option == "--amaf-strength") options.amaf.strength = number;
            else if(option == "--amaf-horizon") options.amaf.horizon = number;
            else options.value_scale = static_cast<float>(number);
            seen = true;
        }
        else if(option == "--amaf-evidence")
        {
            if(engine_name != "amaf-pw" || amaf_evidence_seen || ++i >= argc)
                throw std::invalid_argument("invalid AMAF evidence option");
            const std::string value = argv[i];
            if(value == "unique-leaves") options.amaf.unique_leaf_evidence = true;
            else if(value != "visits") throw std::invalid_argument("unknown AMAF evidence");
            amaf_evidence_seen = true;
        }
        else if(option == "--amaf-application")
        {
            if(engine_name != "amaf-pw" || amaf_application_seen || ++i >= argc)
                throw std::invalid_argument("invalid AMAF application option");
            const std::string value = argv[i];
            if(value == "selection") options.amaf.application = amaf_application::selection;
            else if(value == "admission") options.amaf.application = amaf_application::admission;
            else if(value == "both") options.amaf.application = amaf_application::both;
            else if(value == "children") options.amaf.application = amaf_application::children;
            else throw std::invalid_argument("unknown AMAF application");
            amaf_application_seen = true;
        }
        else if(option == "--amaf-credit")
        {
            if(engine_name != "amaf-pw" || amaf_credit_seen || ++i >= argc)
                throw std::invalid_argument("invalid AMAF credit option");
            const std::string value = argv[i];
            if(value == "blend") options.amaf.credit = amaf_credit::blend;
            else if(value == "centered") options.amaf.credit = amaf_credit::centered;
            else throw std::invalid_argument("unknown AMAF credit");
            amaf_credit_seen = true;
        }
        else if(option == "--neural-usage")
        {
            if(engine_name != "amaf-pw" || neural_usage_seen || ++i >= argc)
                throw std::invalid_argument("invalid neural usage option");
            const std::string value = argv[i];
            if(value != "leaf" && value != "amaf-prior")
                throw std::invalid_argument("unknown neural usage");
            options.amaf.neural_prior_only = value == "amaf-prior";
            neural_usage_seen = true;
        }
        else if(option == "--amaf-decay")
        {
            if(engine_name != "amaf-pw" || amaf_decay_seen || ++i >= argc)
                throw std::invalid_argument("invalid AMAF decay option");
            const std::string value = argv[i];
            if(value == "inverse") options.amaf.decay = amaf_decay::inverse;
            else if(value == "sqrt") options.amaf.decay = amaf_decay::square_root;
            else if(value == "exponential") options.amaf.decay = amaf_decay::exponential;
            else throw std::invalid_argument("unknown AMAF decay");
            amaf_decay_seen = true;
        }
        else if(option == "--model")
        {
            if((engine_name != "neural-pw" && engine_name != "amaf-pw")
                || !options.model_path.empty() || ++i >= argc)
                throw std::invalid_argument("invalid model option");
            options.model_path = argv[i];
            if(options.model_path.empty()) throw std::invalid_argument("empty model path");
        }
        else if(option == "--evaluation-features")
        {
            if(!supports_evaluation_features(engine_name) || evaluation_features_seen
               || ++i >= argc)
                throw std::invalid_argument("invalid evaluation features option");
            options.evaluation_features = argv[i];
            evaluation_features_seen = true;
        }
        else if(option == "--pw-constant")
        {
            if(!supports_progressive_widening(engine_name) || pw_constant_seen
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
            if(!supports_progressive_widening(engine_name) || pw_alpha_seen
               || ++i >= argc)
            {
                throw std::invalid_argument("invalid widening alpha option");
            }
            options.progressive_widening_alpha = parse_positive_float(argv[i]);
            if(options.progressive_widening_alpha > 1.0)
                throw std::invalid_argument("widening alpha exceeds one");
            pw_alpha_seen = true;
        }
        else
        {
            throw std::invalid_argument("unknown option");
        }
    }
    return options;
}
}


void print_engine_usage(std::ostream &out) { print_usage(out); }

std::unique_ptr<engine> make_engine(int argc, const char *argv[],
                                  std::unique_ptr<io_handler> io)
{
    if(argc < 2) throw std::invalid_argument("missing engine name");
    const std::string engine_name = argv[1];
    const auto options = parse_options(argc, argv, engine_name);
    std::unique_ptr<engine> selected_engine;
    if(engine_name == "mcts")
    {
        selected_engine = std::make_unique<mcts_engine>(
            std::move(io), options.seed,
            options.rollout_max_actions);
    }
    else if(engine_name == "mcts-weighted")
    {
        selected_engine = std::make_unique<mcts_weighted_engine>(
            std::move(io), options.seed,
            options.rollout_max_actions, options.weight_temperature);
    }
    else if(engine_name == "zero")
    {
        selected_engine = std::make_unique<zero_engine>(
            std::move(io), options.seed);
    }
    else if(engine_name == "zero-capture")
    {
        selected_engine = std::make_unique<zero_capture_engine>(
            std::move(io), options.seed);
    }
    else if(engine_name == "zero-capture-check-pw")
    {
        selected_engine = std::make_unique<zero_capture_check_pw_engine>(
            std::move(io), options.seed,
            options.progressive_widening_constant, options.progressive_widening_alpha);
    }
    else if(engine_name == "mvvlva")
    {
        selected_engine = std::make_unique<mvvlva_engine>(
            std::move(io), options.seed,
            options.progressive_widening_constant, options.progressive_widening_alpha);
    }
    else if(engine_name == "mvvlva-exposure")
    {
        selected_engine = std::make_unique<mvvlva_exposure_engine>(
            std::move(io), options.seed,
            options.progressive_widening_constant, options.progressive_widening_alpha);
    }
    else if(engine_name == "mvvlva-exposure-pn")
    {
        selected_engine = std::make_unique<mvvlva_exposure_pn_engine>(
            std::move(io), options.seed,
            options.progressive_widening_constant, options.progressive_widening_alpha,
            options.pn_constant);
    }
    else if(engine_name == "amaf-pw")
    {
        std::shared_ptr<const neural_model> model;
        if(!options.model_path.empty())
            model = std::make_shared<const neural_model>(neural_model::load(options.model_path));
        selected_engine = std::make_unique<amaf_pw_engine>(
            std::move(io), options.seed, options.progressive_widening_constant,
            options.progressive_widening_alpha, options.amaf,
            std::move(model), options.value_scale, options.cache_evaluations);
    }
    else if(engine_name == "neural-pw")
    {
        if(options.model_path.empty()) throw std::invalid_argument("neural-pw requires --model");
        selected_engine = std::make_unique<neural_pw_engine>(
            std::move(io), options.seed, options.progressive_widening_constant,
            options.progressive_widening_alpha,
            std::make_shared<const neural_model>(neural_model::load(options.model_path)));
    }
    else if(engine_name == "linear-pw" || engine_name == "linear-safety-pw"
            || engine_name == "linear-trained-pw")
    {
        const auto weights = engine_name == "linear-safety-pw"
            ? linear_cutoff_evaluation::safety_weights()
            : engine_name == "linear-trained-pw"
                ? linear_cutoff_evaluation::trained_weights()
                : linear_cutoff_evaluation::default_weights();
        selected_engine = std::make_unique<linear_pw_engine>(
            std::move(io), options.seed, options.progressive_widening_constant,
            options.progressive_widening_alpha,
            linear_cutoff_evaluation::select_features(weights, options.evaluation_features));
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
                std::move(io), options.seed,
                options.rollout_max_actions, weights,
                options.weight_temperature);
        }
        else
        {
            selected_engine = std::make_unique<linear_engine>(
                std::move(io), options.seed,
                options.rollout_max_actions, weights);
        }
    }
    else if(engine_name == "flat-ucb")
    {
        selected_engine = std::make_unique<flat_ucb_engine>(
            std::move(io), options.seed,
            options.rollout_max_actions);
    }
    else if(engine_name == "flat-ucb-weighted")
    {
        selected_engine = std::make_unique<flat_ucb_weighted_engine>(
            std::move(io), options.seed,
            options.rollout_max_actions, options.weight_temperature);
    }
    else if(engine_name == "monkey")
    {
        selected_engine = std::make_unique<monkey_engine>(
            std::move(io), options.seed);
    }

    if(!selected_engine) throw std::invalid_argument("unknown engine: " + engine_name);
    std::ostringstream description;
    for(int i = 0; i < argc; ++i)
    {
        if(i) description << ' ';
        description << std::quoted(argv[i]);
    }
    selected_engine->set_search_description(description.str());

    return selected_engine;
}
