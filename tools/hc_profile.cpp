#include "hc_profile.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "pgnparser.h"
#include "search_tools.h"
#include "hc_profile_metrics.h"
#include "turn_selection.h"

namespace
{
std::atomic<bool> interrupted = false;
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void interrupt_profile(int)
{
    interrupted.store(true, std::memory_order_relaxed);
}

struct options
{
    search_mode policy = search_mode::deferred;
    std::string policy_name = "deferred";
    int warmup_rounds = 3;
    int rounds = 5;
    bool json = false;
    bool metrics = false;
    std::vector<int> prefixes;
};

struct sample
{
    int round;
    std::size_t position_index;
    std::string outcome;
    double elapsed_ms;
    std::optional<std::size_t> action_count;
    bool exhausted = false;
    std::optional<hc_profile::counters> metrics;
};

struct round_result
{
    int round;
    std::size_t positions;
    double total_ms;
};

struct failure
{
    int round;
    bool warmup;
    std::size_t position_index;
    std::string phase;
    std::string message;
};

struct count_expectation
{
    char comparison;
    int value;

    int limit() const { return value + (comparison == '<' ? 0 : 1); }
    bool matches(std::size_t count) const
    {
        const auto expected = static_cast<std::size_t>(value);
        if(comparison == '<') return count < expected;
        if(comparison == '>') return count > expected;
        return count == expected;
    }
    std::string text() const { return std::string(1, comparison) + std::to_string(value); }
};

struct profile_case
{
    std::string workload;
    std::string file;
    std::optional<pgnparser_ast::game> game;
    std::optional<state> initial;
    std::size_t endpoint_index = 0;
    std::vector<std::string> expected_outcomes;
    std::optional<std::size_t> action_count;
    std::vector<sample> samples;
    std::optional<count_expectation> expected;
    std::optional<turn_selection> at;
    std::optional<bool> exhausted;
    std::optional<int> prefix;
    std::vector<round_result> rounds;
    std::vector<failure> errors;

    profile_case(std::string workload, std::string file)
        : workload{std::move(workload)}, file{std::move(file)} {}

    int limit() const { return prefix.value_or(expected->limit()); }
    bool matches(std::size_t count) const
    {
        if(!prefix) return expected->matches(count);
        const bool exhausted = count < static_cast<std::size_t>(*prefix);
        if(exhausted) return expected->matches(count);
        if(expected->comparison == '=')
            return count <= static_cast<std::size_t>(expected->value);
        if(expected->comparison == '<') return expected->matches(count);
        return true;
    }
};

void print_help()
{
    std::cout
        << "Usage: 5dtools hc-profile [options]\n"
        << "Reads workload records from stdin until EOF:\n"
        << "  PERFTEST|CHECKMATE <single-line 5dpgn>\n"
        << "  EXPECTED<100 COUNT <single-line 5dpgn>\n"
        << "  EXPECTED=100 AT=11b COUNT <single-line 5dpgn>\n"
        << "  AT selects a submitted main-line turn (0 selects the initial position)\n"
        << "  EXPECTED=100 or EXPECTED>100 stops at 101 actions\n"
        << "  --policy <name>         deferred (default), balanced, stable, iterative, fine, naive\n"
        << "  --warmup-rounds <n>     complete unrecorded workload rounds (default: 3)\n"
        << "  --rounds <n>            measured workload rounds (default: 5)\n"
        << "  --prefixes 10,100,1000  add separate bounded COUNT cases (or none)\n"
        << "  --metrics              measure allocations and peak live HCs in a separate pass\n"
        << "                         requires a build with HC_PROFILE_METRICS=ON\n"
        << "  --format text|json      output format (default: text)\n"
        << "  -h, --help              show this help\n"
        << "Ctrl+C prints completed samples and exits with code 130.\n"
        << "Times include search-context construction/destruction and terminal\n"
        << "classification, excluding parsing, replay, validation, and output.\n";
}

int parse_count(std::string_view value, bool allow_zero, std::string_view label = "round count")
{
    int result = 0;
    const auto [end, ec] = std::from_chars(
        value.data(), value.data() + value.size(), result);
    if(ec != std::errc{} || end != value.data() + value.size()
       || result < (allow_zero ? 0 : 1))
        throw std::invalid_argument("invalid " + std::string(label) + ": " + std::string(value));
    return result;
}

options parse_options(int argc, const char *argv[])
{
    options result;
    for(int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if(arg == "--metrics")
        {
            if(!hc_profile::available)
                throw std::invalid_argument("--metrics requires a build with HC_PROFILE_METRICS=ON");
            result.metrics = true;
            continue;
        }
        if(arg != "--policy" && arg != "--warmup-rounds"
           && arg != "--rounds" && arg != "--format" && arg != "--prefixes")
            throw std::invalid_argument("unknown hc-profile option: " + std::string(arg));
        if(++i == argc)
            throw std::invalid_argument(std::string(arg) + " requires a value");
        const std::string_view value = argv[i];
        if(arg == "--warmup-rounds") result.warmup_rounds = parse_count(value, true);
        else if(arg == "--rounds") result.rounds = parse_count(value, false);
        else if(arg == "--prefixes")
        {
            result.prefixes.clear();
            if(value == "none") continue;
            std::size_t start = 0;
            while(true)
            {
                const auto comma = value.find(',', start);
                const auto prefix = parse_count(value.substr(start, comma - start), false, "action prefix");
                if(std::find(result.prefixes.begin(), result.prefixes.end(), prefix) != result.prefixes.end())
                    throw std::invalid_argument("duplicate action prefix");
                result.prefixes.push_back(prefix);
                if(comma == std::string_view::npos) break;
                start = comma + 1;
            }
        }
        else if(arg == "--format")
        {
            if(value != "text" && value != "json")
                throw std::invalid_argument("format must be text or json");
            result.json = value == "json";
        }
        else
        {
            result.policy_name = value;
            if(value == "balanced") result.policy = search_mode::balanced;
            else if(value == "stable") result.policy = search_mode::stable;
            else if(value == "deferred") result.policy = search_mode::deferred;
            else if(value == "iterative") result.policy = search_mode::iterative;
            else if(value == "fine") result.policy = search_mode::fine;
            else if(value == "naive") result.policy = search_mode::naive;
            else throw std::invalid_argument("unknown search policy: " + std::string(value));
        }
    }
    return result;
}

const pgnparser_ast::gametree::variation_t *next_action(
    const pgnparser_ast::gametree &node)
{
    const auto *variations = std::get_if<pgnparser_ast::gametree::variations_t>(
        &node.variations_or_outcome);
    return variations && !variations->empty() ? &variations->back() : nullptr;
}

void prepare(profile_case &entry, std::mutex &results_mutex, std::string_view source)
{
    try
    {
        auto parsed = pgnparser(std::string(source)).parse_game();
        if(!parsed) throw std::runtime_error("expected a PGN game");
        entry.game = std::move(*parsed);
        if(entry.workload != "profile-perftest")
        {
            auto *node = &entry.game->gt;
            while(const auto *action = next_action(*node))
            {
                if(entry.at && entry.endpoint_index == entry.at->actions) break;
                ++entry.endpoint_index;
                node = action->second.get();
            }
            if(entry.at)
            {
                if(entry.endpoint_index != entry.at->actions)
                    throw std::invalid_argument("turn " + entry.at->label
                        + " is not available; the last submitted main-line turn is "
                        + submitted_turn(entry.endpoint_index));
                *node = pgnparser_ast::gametree{};
            }
            entry.initial = state::from_ast(*entry.game);
        }
        else
        {
            auto mainline = std::move(entry.game->gt);
            entry.game->gt = pgnparser_ast::gametree{};
            entry.initial = state::from_ast(*entry.game);
            entry.game->gt = std::move(mainline);
        }
    }
    catch(const std::exception &error)
    {
        std::lock_guard lock(results_mutex);
        entry.errors.push_back({0, false, 0, "prepare", error.what()});
    }
}

void read_records(std::vector<profile_case> &cases, std::mutex &results_mutex,
    const options &settings)
{
    std::string line;
    std::size_t line_number = 0;
    while(std::getline(std::cin, line))
    {
        ++line_number;
        std::istringstream record(line);
        std::string command;
        if(!(record >> command)) continue;
        const auto label = "stdin:" + std::to_string(line_number);
        std::optional<count_expectation> expected;
        if(command.starts_with("EXPECTED"))
        {
            const std::string_view specification(command);
            if(specification.size() < 10 || (specification[8] != '<'
                && specification[8] != '=' && specification[8] != '>'))
                throw std::invalid_argument(label + ": use EXPECTED<n, EXPECTED=n, or EXPECTED>n");
            const auto value = specification.substr(9);
            int count = 0;
            const auto [end, ec] = std::from_chars(
                value.data(), value.data() + value.size(), count);
            const char comparison = specification[8];
            if(value.front() < '0' || value.front() > '9'
                || ec != std::errc{} || end != value.data() + value.size() || count < 0
                || (comparison == '<' ? count == 0 : count == std::numeric_limits<int>::max()))
                throw std::invalid_argument(label + ": invalid expected count");
            expected = count_expectation{comparison, count};
            record >> command;
        }
        std::optional<turn_selection> at;
        if(command.starts_with("AT="))
        {
            std::string error;
            at = parse_turn_selection(std::string_view(command).substr(3), error);
            if(!at) throw std::invalid_argument(label + ": " + error);
            record >> command;
        }
        std::string workload;
        if(command == "COUNT") workload = "profile-count";
        else if(command == "PERFTEST") workload = "profile-perftest";
        else if(command == "CHECKMATE") workload = "profile-checkmate";
        else throw std::invalid_argument(label + ": unknown workload: " + command);
        if(expected && command != "COUNT")
            throw std::invalid_argument(label + ": EXPECTED requires COUNT");
        if(at && command != "COUNT")
            throw std::invalid_argument(label + ": AT requires COUNT");
        if(command == "COUNT" && !expected)
            throw std::invalid_argument(label + ": COUNT requires a bounded EXPECTED comparison");
        std::string source;
        std::getline(record >> std::ws, source);
        if(source.empty()) throw std::invalid_argument(label + ": missing 5dpgn");
        if(expected)
        {
            for(int prefix : settings.prefixes)
            {
                auto &bounded = cases.emplace_back("profile-prefix", label);
                bounded.expected = expected;
                bounded.prefix = prefix;
                bounded.at = at;
                prepare(bounded, results_mutex, source);
            }
        }
        auto &entry = cases.emplace_back(workload, label);
        entry.expected = expected;
        entry.at = at;
        prepare(entry, results_mutex, source);
    }
    if(std::cin.bad()) throw std::runtime_error("cannot read stdin");
    if(cases.empty()) throw std::invalid_argument("provide at least one workload record");
}

void replay_action(state &current, const pgnparser_ast::actions &action,
    const std::shared_ptr<const std::string> &source)
{
    for(const auto &move : action.moves) current.apply_move_ast(move, source);
    if(!current.test_present())
        throw pgn_validation_error(pgn_validation_error::present_not_advanced{},
            pgn_validation_error::make_subject(action, source));
    current.submit<true>();
    if(auto exposure = current.find_royal_exposure(false))
        throw pgn_validation_error(*exposure,
            pgn_validation_error::make_subject(action, source));
}

void run_round(profile_case &entry, const options &settings, int round, bool warmup,
    std::mutex &results_mutex)
{
    if(!entry.errors.empty()) return;
    std::size_t position_index = entry.workload == "profile-perftest"
        ? 0 : entry.endpoint_index;
    std::string phase = "setup";
    try
    {
        state current = *entry.initial;
        const auto *node = &entry.game->gt;
        double total_ms = 0;
        std::size_t positions = 0;
        while(true)
        {
            phase = "search";
            const auto start = std::chrono::steady_clock::now();
            std::optional<moveseq> moves;
            std::optional<std::size_t> action_count;
            if(entry.expected)
                action_count = count_actions(current, settings.policy, entry.limit());
            else
                moves = find_first_action(current, settings.policy);
            const bool has_actions = action_count ? *action_count != 0 : moves.has_value();
            const bool checkmate = !has_actions && current.has_phantom_check();
            const double elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            std::optional<hc_profile::counters> metrics;
            if(settings.metrics && !warmup)
            {
                phase = "metrics";
                hc_profile::measurement measurement;
                if(action_count)
                {
                    if(count_actions(current, settings.policy, entry.limit()) != *action_count)
                        throw std::runtime_error("diagnostic enumeration count differs from timed search");
                }
                else if(find_first_action(current, settings.policy).has_value() != moves.has_value())
                    throw std::runtime_error("diagnostic action availability differs from timed search");
                if(!has_actions) (void)current.has_phantom_check();
                metrics = measurement.finish();
                if(metrics->live_hcs != 0)
                    throw std::runtime_error("HC objects outlived diagnostic search");
            }
            // All checks below are outside the measured operation.
            phase = "validation";
            const std::string outcome = has_actions ? "action"
                : checkmate ? "checkmate" : "stalemate";
            if(moves && !current.can_apply(action::from_moveseq(*moves, current)))
                throw std::runtime_error("search returned an illegal action");
            const auto *next = entry.workload == "profile-perftest"
                ? next_action(*node) : nullptr;
            if(!has_actions && next)
                throw std::runtime_error("search reports a terminal position before the recorded main-line end");
            {
                std::lock_guard lock(results_mutex);
                if(action_count)
                {
                    if(entry.action_count && entry.action_count != action_count)
                        throw std::runtime_error("action count changed between rounds");
                    entry.action_count = action_count;
                    entry.exhausted = *action_count < static_cast<std::size_t>(entry.limit());
                    if(!entry.matches(*action_count))
                        throw std::runtime_error("expected " + entry.expected->text()
                            + " actions, observed " + std::to_string(*action_count)
                            + (*entry.exhausted ? " (exhausted)" : " (limit reached)"));
                }
                if(positions == entry.expected_outcomes.size())
                    entry.expected_outcomes.push_back(outcome);
                else if(entry.expected_outcomes.at(positions) != outcome)
                    throw std::runtime_error("action availability or mate classification changed between rounds");
                if(!warmup)
                    entry.samples.push_back({round, position_index, outcome, elapsed_ms,
                        action_count, entry.exhausted.value_or(false), metrics});
            }
            ++positions;
            total_ms += elapsed_ms;
            if(!next) break;
            phase = "replay";
            replay_action(current, next->first, entry.game->source_text);
            node = next->second.get();
            ++position_index;
        }
        std::lock_guard lock(results_mutex);
        if(positions != entry.expected_outcomes.size())
            throw std::runtime_error("position count changed between rounds");
        if(!warmup) entry.rounds.push_back({round, positions, total_ms});
    }
    catch(const std::exception &error)
    {
        std::lock_guard lock(results_mutex);
        entry.errors.push_back({round, warmup, position_index, phase, error.what()});
    }
}

struct statistics
{
    std::size_t count = 0;
    double total = 0, mean = 0, median = 0, p95 = 0, maximum = 0;
};

statistics summarize(std::vector<double> values)
{
    if(values.empty()) return {};
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    const double total = std::accumulate(values.begin(), values.end(), 0.0);
    const double median = n % 2 ? values[n / 2]
        : (values[n / 2 - 1] + values[n / 2]) / 2;
    return {n, total, total / n, median,
        values[static_cast<std::size_t>(std::ceil(0.95 * n)) - 1], values.back()};
}

std::vector<double> times(const profile_case &entry)
{
    std::vector<double> result;
    for(const auto &value : entry.samples) result.push_back(value.elapsed_ms);
    return result;
}

std::vector<double> metric_values(const profile_case &entry, bool allocations)
{
    std::vector<double> result;
    for(const auto &sample : entry.samples)
        if(sample.metrics) result.push_back(allocations
            ? sample.metrics->allocations : sample.metrics->peak_live_hcs);
    return result;
}

void json_string(std::ostream &out, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for(unsigned char c : value)
    {
        if(c == '"' || c == '\\') out << '\\' << char(c);
        else if(c < 0x20) out << "\\u00" << hex[c / 16] << hex[c % 16];
        else out << char(c);
    }
    out << '"';
}

void json_statistics(std::ostream &out, const statistics &s)
{
    out << "{\"samples\":" << s.count << ",\"total_ms\":" << s.total;
    const auto field = [&](const char *name, double value) {
        out << ",\"" << name << "\":";
        if(s.count) out << value;
        else out << "null";
    };
    field("mean_ms", s.mean);
    field("median_ms", s.median);
    field("p95_ms", s.p95);
    field("max_ms", s.maximum);
    out << '}';
}

bool complete(const profile_case &entry, const options &settings)
{
    return entry.errors.empty() && entry.rounds.size() == static_cast<std::size_t>(settings.rounds);
}

void json_metrics(const profile_case &entry)
{
    std::cout << ",\"metrics\":{";
    for(bool allocations : {true, false})
    {
        if(!allocations) std::cout << ',';
        const auto s = summarize(metric_values(entry, allocations));
        std::cout << (allocations ? "\"allocations\":" : "\"peak_live_hcs\":")
                  << "{\"samples\":" << s.count;
        const auto field = [&](const char *name, double value) {
            std::cout << ",\"" << name << "\":";
            if(s.count) std::cout << value;
            else std::cout << "null";
        };
        field("mean", s.mean);
        field("median", s.median);
        field("p95", s.p95);
        field("max", s.maximum);
        std::cout << '}';
    }
    std::cout << '}';
}

void print_results(const options &settings, const std::vector<profile_case> &cases, bool was_interrupted)
{
    std::cout << std::setprecision(12);
    if(settings.json)
    {
        std::cout << "{\"schema_version\":1,\"policy\":";
        json_string(std::cout, settings.policy_name);
        std::cout << ",\"warmup_rounds\":" << settings.warmup_rounds
                  << ",\"rounds\":" << settings.rounds
                  << ",\"metrics_enabled\":" << (settings.metrics ? "true" : "false")
                  << ",\"metrics_available\":" << (hc_profile::available ? "true" : "false")
                  << ",\"interrupted\":" << (was_interrupted ? "true" : "false");
#ifdef PROJECT_VERSION_STRING
        std::cout << ",\"version\":";
        json_string(std::cout, PROJECT_VERSION_STRING);
#endif
#ifdef NDEBUG
        std::cout << ",\"assertions_enabled\":false";
#else
        std::cout << ",\"assertions_enabled\":true";
#endif
        std::cout << ",\"cases\":[";
        bool first = true;
        for(const auto &entry : cases)
        {
            if(!std::exchange(first, false)) std::cout << ',';
            std::cout << "{\"workload\":";
            json_string(std::cout, entry.workload);
            std::cout << ",\"file\":";
            json_string(std::cout, entry.file);
            if(entry.at)
            {
                std::cout << ",\"at\":";
                json_string(std::cout, entry.at->label);
            }
            if(entry.expected)
            {
                if(entry.prefix) std::cout << ",\"requested_actions\":" << *entry.prefix;
                std::cout << ",\"action_count\":";
                if(entry.action_count) std::cout << *entry.action_count;
                else std::cout << "null";
                std::cout << ",\"expected_count\":";
                std::cout << entry.expected->value << ",\"expected_operator\":";
                json_string(std::cout, std::string(1, entry.expected->comparison));
                std::cout << ",\"action_limit\":" << entry.limit() << ",\"exhausted\":";
                if(entry.exhausted) std::cout << (*entry.exhausted ? "true" : "false");
                else std::cout << "null";
            }
            std::cout << ",\"complete\":" << (complete(entry, settings) ? "true" : "false")
                      << ",\"positions_per_round\":" << entry.expected_outcomes.size()
                      << ",\"summary\":";
            json_statistics(std::cout, summarize(times(entry)));
            if(settings.metrics) json_metrics(entry);
            std::cout << ",\"samples\":[";
            bool first_sample = true;
            for(const auto &s : entry.samples)
            {
                if(!std::exchange(first_sample, false)) std::cout << ',';
                std::cout << "{\"round\":" << s.round << ",\"position_index\":"
                          << s.position_index << ",\"outcome\":";
                json_string(std::cout, s.outcome);
                if(s.action_count)
                    std::cout << ",\"action_count\":" << *s.action_count
                              << ",\"exhausted\":" << (s.exhausted ? "true" : "false");
                if(s.metrics)
                    std::cout << ",\"allocations\":" << s.metrics->allocations
                              << ",\"peak_live_hcs\":" << s.metrics->peak_live_hcs;
                std::cout << ",\"elapsed_ms\":" << s.elapsed_ms << '}';
            }
            std::cout << "],\"game_rounds\":[";
            bool first_round = true;
            for(const auto &r : entry.rounds)
            {
                if(!std::exchange(first_round, false)) std::cout << ',';
                std::cout << "{\"round\":" << r.round << ",\"positions\":"
                          << r.positions << ",\"total_ms\":" << r.total_ms << '}';
            }
            std::cout << "],\"errors\":[";
            bool first_error = true;
            for(const auto &error : entry.errors)
            {
                if(!std::exchange(first_error, false)) std::cout << ',';
                std::cout << "{\"round\":" << error.round << ",\"warmup\":"
                          << (error.warmup ? "true" : "false")
                          << ",\"position_index\":" << error.position_index << ",\"phase\":";
                json_string(std::cout, error.phase);
                std::cout << ",\"message\":";
                json_string(std::cout, error.message);
                std::cout << '}';
            }
            std::cout << "]}";
        }
        std::cout << "],\"summary\":{";
    }
    else
        std::cout << "HC profile: " << settings.policy_name << ", " << settings.warmup_rounds
                  << " warmup rounds, " << settings.rounds << " measured rounds\n";

    bool first_workload = true;
    for(const std::string workload : {"profile-checkmate", "profile-perftest", "profile-prefix", "profile-count"})
    {
        std::vector<double> values;
        std::size_t failed_cases = 0, incomplete_cases = 0;
        for(const auto &entry : cases)
        {
            if(entry.workload != workload) continue;
            auto entry_times = times(entry);
            values.insert(values.end(), entry_times.begin(), entry_times.end());
            failed_cases += !entry.errors.empty();
            incomplete_cases += !complete(entry, settings);
            if(!settings.json)
            {
                const auto s = summarize(std::move(entry_times));
                std::cout << workload << ' ' << entry.file << ": positions="
                          << entry.expected_outcomes.size() << " samples=" << s.count
                          << " outcome=" << (entry.expected_outcomes.empty()
                              ? "unknown" : entry.expected_outcomes.back())
                          << (entry.expected
                              ? " action_count=" + (entry.action_count
                                  ? std::to_string(*entry.action_count) : "unknown") : "")
                          << (entry.expected ? " expected" + entry.expected->text()
                              + " action_limit=" + std::to_string(entry.limit())
                              + " exhausted=" + (entry.exhausted
                                  ? (*entry.exhausted ? "true" : "false") : "unknown") : "")
                          << " mean_ms=" << s.mean << " median_ms=" << s.median
                          << " p95_ms=" << s.p95 << " max_ms=" << s.maximum;
                if(entry.at) std::cout << " at=" << entry.at->label;
                if(settings.metrics)
                    std::cout << " allocations_median=" << summarize(metric_values(entry, true)).median
                              << " peak_live_hcs_max=" << summarize(metric_values(entry, false)).maximum;
                std::cout << (complete(entry, settings) ? "\n" : " INCOMPLETE\n");
                for(const auto &error : entry.errors)
                    std::cout << "  " << error.phase << ": " << error.message << '\n';
            }
        }
        if(settings.json)
        {
            if(!std::exchange(first_workload, false)) std::cout << ',';
            json_string(std::cout, workload);
            std::cout << ":{\"complete\":" << (incomplete_cases ? "false" : "true")
                      << ",\"failed_cases\":" << failed_cases
                      << ",\"incomplete_cases\":" << incomplete_cases << ",\"timing\":";
            json_statistics(std::cout, summarize(std::move(values)));
            std::cout << '}';
        }
        else if(!values.empty() || incomplete_cases)
        {
            const auto s = summarize(std::move(values));
            std::cout << workload << " TOTAL: samples=" << s.count << " mean_ms="
                      << s.mean << " median_ms=" << s.median << " p95_ms=" << s.p95
                      << " max_ms=" << s.maximum << " total_ms=" << s.total
                      << " failed_cases=" << failed_cases << " incomplete_cases="
                      << incomplete_cases << '\n';
        }
    }
    if(settings.json) std::cout << "}}\n";
    else if(was_interrupted) std::cout << "Interrupted: partial results (unfinished searches omitted).\n";
}
} /* anonymous namespace */

int run_hc_profile(int argc, const char *argv[])
{
    if(argc == 2 && (std::string_view(argv[1]) == "--help"
                    || std::string_view(argv[1]) == "-h"))
    {
        print_help();
        return 0;
    }
    options settings;
    try { settings = parse_options(argc, argv); }
    catch(const std::invalid_argument &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
    std::vector<profile_case> cases;
    std::mutex results_mutex;
    try
    {
        read_records(cases, results_mutex, settings);
    }
    catch(const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
    interrupted = false;
    const auto previous_handler = std::signal(SIGINT, interrupt_profile);
    if(previous_handler == SIG_ERR)
        throw std::runtime_error("cannot install profiling interrupt handler");
#ifdef SIGBREAK
    const auto previous_break_handler = std::signal(SIGBREAK, interrupt_profile);
#endif
    std::atomic<bool> finished = false;
    std::exception_ptr worker_error;
    // Keep the search itself unchanged, including policies without cancellation.
    // Only the main thread handles interruption and prints published results.
    std::thread worker([&] {
        try
        {
            for(int round = 0; round < settings.warmup_rounds; ++round)
            {
                std::cerr << "Warmup round " << round + 1 << '/' << settings.warmup_rounds << '\n';
                for(auto &entry : cases) run_round(entry, settings, round + 1, true, results_mutex);
            }
            for(int round = 0; round < settings.rounds; ++round)
            {
                std::cerr << "Measured round " << round + 1 << '/' << settings.rounds << '\n';
                for(auto &entry : cases) run_round(entry, settings, round + 1, false, results_mutex);
            }
        }
        catch(...) { worker_error = std::current_exception(); }
        finished = true;
    });
    while(!finished && !interrupted)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if(interrupted)
    {
        std::lock_guard lock(results_mutex);
        print_results(settings, cases, true);
        std::cout.flush();
        // A search may not support cancellation. End the process without
        // destroying objects still used by the worker or waiting for that search.
        std::_Exit(130);
    }
    worker.join();
    std::signal(SIGINT, previous_handler);
#ifdef SIGBREAK
    std::signal(SIGBREAK, previous_break_handler);
#endif
    if(worker_error) std::rethrow_exception(worker_error);
    print_results(settings, cases, false);
    return std::any_of(cases.begin(), cases.end(),
        [](const profile_case &entry) { return !entry.errors.empty(); }) ? 1 : 0;
}
