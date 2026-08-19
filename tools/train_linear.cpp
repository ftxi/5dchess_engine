#include "train_linear.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "linear_training.h"

namespace
{

void print_usage(std::ostream &output)
{
    output
        << "usage: 5dtools train-linear [options] <training-data> <output-weights>\n"
        << "\nOptions:\n"
        << "  --validation <file>       validation samples for checkpointing\n"
        << "  --initial-weights <file>  initialize from a saved checkpoint\n"
        << "  --zero-init               initialize every weight to zero\n"
        << "  --epochs <n>              training epochs (default 100)\n"
        << "  --batch-size <n>          Adam mini-batch size (default 64)\n"
        << "  --learning-rate <x>       Adam learning rate (default 0.01)\n"
        << "  --l2 <x>                  non-bias L2 coefficient (default 0.0001)\n"
        << "  --seed <n>                shuffle seed (default 0)\n"
        << "  --no-shuffle              preserve input order between epochs\n"
        << "  --patience <n>            validation early-stopping patience\n"
        << "  --min-delta <x>           required validation loss improvement\n";
}

std::size_t parse_size(const std::string &text, const char *name)
{
    if(text.empty() || text.front() == '-')
    {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if(consumed != text.size()
       || value > std::numeric_limits<std::size_t>::max())
    {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return static_cast<std::size_t>(value);
}

double parse_double(const std::string &text, const char *name)
{
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if(consumed != text.size())
    {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return value;
}

std::vector<linear_training_sample> load_samples(const std::string &path)
{
    std::ifstream input(path);
    if(!input)
    {
        throw std::runtime_error("cannot open sample file: " + path);
    }
    return read_linear_training_samples(input);
}

linear_engine::weight_vector_t load_weights(const std::string &path)
{
    std::ifstream input(path);
    if(!input)
    {
        throw std::runtime_error("cannot open weight file: " + path);
    }
    return read_linear_weights(input);
}

void print_metrics(const char *name, const linear_training_metrics &metrics)
{
    std::cout << name
              << " objective=" << metrics.objective_loss
              << " mse=" << metrics.mean_squared_error
              << " mae=" << metrics.mean_absolute_error
              << " samples=" << metrics.sample_count << '\n';
}

} /* anonymous namespace */

int run_train_linear(int argc, const char *argv[])
{
    if(argc == 2 && (std::string_view(argv[1]) == "-h"
                     || std::string_view(argv[1]) == "--help"))
    {
        print_usage(std::cout);
        return 0;
    }

    linear_training_options options;
    std::optional<std::string> validation_path;
    std::optional<std::string> initial_weights_path;
    bool zero_init = false;
    std::vector<std::string> positional;
    for(int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        const auto value = [&](const char *name) -> std::string
        {
            if(++i >= argc)
            {
                throw std::invalid_argument(std::string("missing value for ") + name);
            }
            return argv[i];
        };

        if(argument == "--validation")
        {
            validation_path = value("--validation");
        }
        else if(argument == "--initial-weights")
        {
            initial_weights_path = value("--initial-weights");
        }
        else if(argument == "--zero-init")
        {
            zero_init = true;
        }
        else if(argument == "--epochs")
        {
            options.epochs = parse_size(value("--epochs"), "epoch count");
        }
        else if(argument == "--batch-size")
        {
            options.batch_size = parse_size(value("--batch-size"), "batch size");
        }
        else if(argument == "--learning-rate")
        {
            options.learning_rate = parse_double(
                value("--learning-rate"), "learning rate");
        }
        else if(argument == "--l2")
        {
            options.l2 = parse_double(value("--l2"), "L2 coefficient");
        }
        else if(argument == "--seed")
        {
            const std::size_t seed = parse_size(value("--seed"), "seed");
            if(seed > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("seed exceeds uint32 range");
            }
            options.seed = static_cast<std::uint32_t>(seed);
        }
        else if(argument == "--no-shuffle")
        {
            options.shuffle = false;
        }
        else if(argument == "--patience")
        {
            options.early_stopping_patience = parse_size(
                value("--patience"), "patience");
        }
        else if(argument == "--min-delta")
        {
            options.early_stopping_min_delta = parse_double(
                value("--min-delta"), "minimum delta");
        }
        else if(!argument.empty() && argument.front() == '-')
        {
            throw std::invalid_argument("unknown option: " + argument);
        }
        else
        {
            positional.push_back(argument);
        }
    }

    if(positional.size() != 2 || (zero_init && initial_weights_path))
    {
        print_usage(std::cerr);
        throw std::invalid_argument(
            positional.size() != 2
                ? "expected training-data and output-weights paths"
                : "--zero-init and --initial-weights are mutually exclusive");
    }

    const std::vector<linear_training_sample> training
        = load_samples(positional[0]);
    const std::vector<linear_training_sample> validation
        = validation_path ? load_samples(*validation_path)
                          : std::vector<linear_training_sample>{};
    linear_engine::weight_vector_t weights{};
    if(initial_weights_path)
    {
        weights = load_weights(*initial_weights_path);
    }
    else if(!zero_init)
    {
        weights = linear_engine::default_weights();
    }

    const linear_trainer trainer(options);
    const linear_training_report report = trainer.fit(
        training, weights, validation);
    print_metrics("initial-train", report.initial_training);
    print_metrics("final-train", report.final_training);
    if(report.initial_validation && report.final_validation)
    {
        print_metrics("initial-validation", *report.initial_validation);
        print_metrics("final-validation", *report.final_validation);
        std::cout << "best-epoch=" << report.best_epoch
                  << " stopped-early=" << (report.stopped_early ? "yes" : "no")
                  << '\n';
    }

    std::ofstream output(positional[1]);
    if(!output)
    {
        throw std::runtime_error("cannot open output weight file: " + positional[1]);
    }
    write_linear_weights(output, weights);
    return 0;
}
