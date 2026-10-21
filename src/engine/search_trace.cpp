#include "search_trace.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

thread_local search_trace *search_trace::current = nullptr;

std::string search_trace_position(const state &position)
{
    const auto [width, height] = position.get_board_size();
    std::ostringstream out;
    out << "[Board \"Custom\"]\n[Size \"" << width << 'x' << height
        << "\"]\n[Timeline \""
        << (position.get_initial_lines_range().first == -1 ? "even" : "odd")
        << "\"]\n[Promotions \""
        << format_promotions(position.get_promotion_options()) << "\"]\n"
        << position.show_fen();
    return out.str();
}

search_trace::search_trace(const state &position, std::stop_token token,
    std::optional<std::chrono::steady_clock::time_point> until, std::string config)
    : previous(current), root(position), stop(token), deadline(until),
      configuration(std::move(config))
{
    // Diagnostics must never prevent a search from running.
    try
    {
        const char *path = std::getenv("FIVEDCHESS_SEARCH_TRACE_DIR");
        if(path && *path)
        {
            directory = path;
            if(const char *value = std::getenv("FIVEDCHESS_SEARCH_TRACE_MS"))
            {
                const double parsed = std::stod(value);
                if(std::isfinite(parsed) && parsed >= 0) threshold_ms = parsed;
            }
        }
    }
    catch(...) { directory.clear(); }
    started = enabled() ? std::chrono::steady_clock::now()
                        : std::chrono::steady_clock::time_point{};
    current = enabled() ? this : nullptr;
}

search_trace::~search_trace()
{
    if(!records.empty()) record("search", std::chrono::steady_clock::now() - started, nullptr, "");
    current = previous;
    if(records.empty()) return;
    // Flush after search completion so disk I/O cannot consume its budget.
    try
    {
        static std::atomic<std::size_t> sequence{0};
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::filesystem::create_directories(directory);
        for(;;)
        {
            prefix = "search-" + std::to_string(stamp) + "-"
                + std::to_string(sequence.fetch_add(1));
            if(std::filesystem::create_directory(directory / prefix)) break;
        }
        directory /= prefix;
        std::ofstream(directory / "root.5dpgn") << search_trace_position(root);
        for(std::size_t i = 0; i < records.size(); ++i)
        {
            const auto &r = records[i];
            const auto stem = std::to_string(i);
            std::ofstream out(directory / (stem + ".txt"));
            out << std::setprecision(17) << "phase=" << r.phase << "\niteration=" << r.iteration
                << "\nduration_ms=" << r.duration_ms << "\nsearch_elapsed_ms=" << r.elapsed_ms
                << "\nstop_requested=" << r.stopped;
            if(deadline) out << "\ndeadline_overshoot_ms=" << r.overshoot_ms;
            out << "\n" << configuration << '\n' << r.details << '\n';
            if(r.position)
                std::ofstream(directory / (stem + ".5dpgn")) << search_trace_position(*r.position);
        }
    }
    catch(...) {} // Diagnostics must not change search results.
}

void search_trace::record(const char *phase,
    std::chrono::steady_clock::duration duration, const state *position,
    const std::string &details) noexcept
{
    const double ms = std::chrono::duration<double, std::milli>(duration).count();
    // Bound diagnostic memory and output even when every iteration is slow.
    if(!enabled() || ms < threshold_ms || records.size() >= 64) return;
    try
    {
        const auto now = std::chrono::steady_clock::now();
        records.push_back({phase, ms,
            std::chrono::duration<double, std::milli>(now - started).count(),
            deadline ? std::max(0.0,
                std::chrono::duration<double, std::milli>(now - *deadline).count()) : 0,
            stop.stop_requested(), iteration,
            position ? std::optional<state>{*position} : std::nullopt, details});
    }
    catch(...) {}
}

search_trace_phase::search_trace_phase(const char *phase, const state *input) : name(phase)
{
    if(!trace) return;
    started = std::chrono::steady_clock::now();
    try { if(input) position.emplace(*input); }
    catch(...) { trace = nullptr; }
}

search_trace_phase::~search_trace_phase()
{
    if(trace) trace->record(name, std::chrono::steady_clock::now() - started,
                           position ? &*position : nullptr, details);
}
