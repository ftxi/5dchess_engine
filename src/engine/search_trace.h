#ifndef SEARCH_TRACE_H
#define SEARCH_TRACE_H

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>
#include <utility>

#include "state.h"

// Opt-in diagnostics, confined to the search thread. Disabled scopes do not
// copy positions or read the clock at candidate boundaries.
class search_trace
{
    search_trace *previous;
    const state &root;
    std::stop_token stop;
    std::filesystem::path directory;
    double threshold_ms = 10;
    std::chrono::steady_clock::time_point started;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    std::string prefix;
    std::string configuration;
    std::size_t iteration = 0;
    struct slow_record
    {
        std::string phase;
        double duration_ms, elapsed_ms, overshoot_ms;
        bool stopped;
        std::size_t iteration;
        std::optional<state> position;
        std::string details;
    };
    std::vector<slow_record> records;

public:
    static thread_local search_trace *current;
    search_trace(const state &root, std::stop_token stop,
                 std::optional<std::chrono::steady_clock::time_point> deadline,
                 std::string configuration = {});
    ~search_trace();
    search_trace(const search_trace &) = delete;
    search_trace &operator=(const search_trace &) = delete;
    bool enabled() const noexcept { return !directory.empty(); }
    void set_configuration(std::string value) { configuration = std::move(value); }
    void set_iteration(std::size_t value) noexcept { iteration = value; }
    void record(const char *phase, std::chrono::steady_clock::duration duration,
                const state *position, const std::string &details) noexcept;
};

class search_trace_phase
{
    search_trace *trace = search_trace::current;
    const char *name;
    std::chrono::steady_clock::time_point started;
    std::optional<state> position;
    std::string details;

public:
    explicit search_trace_phase(const char *name, const state *position = nullptr);
    ~search_trace_phase();
    search_trace_phase(const search_trace_phase &) = delete;
    search_trace_phase &operator=(const search_trace_phase &) = delete;
    bool enabled() const noexcept { return trace != nullptr; }
    void set_details(std::string value) { details = std::move(value); }
};

// PGN setup with all historical boards and unmoved flags, suitable for replay.
std::string search_trace_position(const state &position);

#endif
