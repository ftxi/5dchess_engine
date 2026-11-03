#undef NDEBUG
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "mcts_engines.h"
#include "flat_ucb.h"
#include "search_control.h"
#include "search_trace.h"

using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace
{
// Bounded waits fail instead of hanging the test suite on broken propagation.
bool wait_for_stop(std::stop_token token)
{
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    wake.wait_for(lock, token, 2s, [] { return false; });
    return token.stop_requested();
}

void test_cancellation_scope()
{
    std::stop_source external;
    {
        search_stop_scope scope(external.get_token(), std::nullopt);
        assert(!scope.get_token().stop_requested());
        external.request_stop();
        assert(scope.get_token().stop_requested());
    }
    search_stop_scope prestopped(external.get_token(), clock_type::now() + 1h);
    assert(prestopped.get_token().stop_requested());
    search_stop_scope expired({}, clock_type::now() - 1ms);
    assert(expired.get_token().stop_requested());

    search_stop_scope timed({}, clock_type::now() + 20ms);
    assert(wait_for_stop(timed.get_token()));
    assert(timed.deadline_expired());

    std::stop_token retained;
    const auto start = clock_type::now();
    {
        search_stop_scope early({}, clock_type::now() + 1h);
        retained = early.get_token();
    }
    assert(clock_type::now() - start < 1s);
    assert(!retained.stop_requested()); // Timer shutdown is not search cancellation.
    try
    {
        search_stop_scope unwinding({}, clock_type::now() + 1h);
        throw std::runtime_error("test");
    }
    catch(const std::runtime_error &) {}
    assert(clock_type::now() - start < 1s);

    // Repeated external-stop/timer-destruction races must not deadlock.
    for(int i = 0; i < 100; ++i)
    {
        std::stop_source source;
        std::jthread stopper;
        {
            search_stop_scope scope(source.get_token(), clock_type::now() + 1h);
            stopper = std::jthread([source]() mutable { source.request_stop(); });
        }
    }
}

struct sink_io : io_handler
{
    std::string read_line() override { return {}; }
    void write_line(const std::string &) override {}
    bool is_open() override { return false; }
};

struct blocking_evaluation
{
    using result_type = reward_t<rollout_details>;
    bool *entered;
    bool *cancelled;
    template<class Observer>
    std::optional<result_type> evaluate(state, std::stop_token stop, Observer &)
    {
        *entered = true;
        *cancelled = wait_for_stop(stop);
        return std::nullopt;
    }
};

struct counting_backpropagation
{
    int *count;
    template<class Node, class Result, class Observer>
    void backpropagate(Node *, const Result &, Observer &) { ++*count; }
};

struct fallback_selection
{
    bool *called;
    template<class Node, class Observer>
    Node *select_ceiling(Node *node, Observer &)
    {
        *called = true;
        assert(node->get_info().visits == 0);
        return finetree_nearby_ceiling(node);
    }
};

void test_deadline_inside_evaluation(bool use_external_stop)
{
    bool entered = false, cancelled = false, selected = false;
    int committed = 0;
    using test_engine = basic_mcts_engine<uct_tree_policy<natural_HC_ordering>,
        blocking_evaluation, counting_backpropagation, fallback_selection, mcts_observer>;
    test_engine engine({&entered, &cancelled}, uct_tree_policy<natural_HC_ordering>{}, {&committed}, {&selected}, {},
                       std::make_unique<sink_io>());
    engine.set_position("startpos", "");
    std::stop_source external;
    std::jthread stopper;
    if(use_external_stop)
        stopper = std::jthread([&] {
            std::this_thread::sleep_for(100ms);
            external.request_stop();
        });
    const auto start = clock_type::now();
    auto best = engine.search(std::nullopt,
        use_external_stop ? std::optional<int>{} : std::optional<int>{100}, external.get_token());
    assert(entered && cancelled && selected);
    assert(committed == 0);
    assert(best && engine.get_current_state()->can_apply(*best));
    assert(clock_type::now() - start < 1s);
    if(!use_external_stop) assert(!external.stop_requested());
}

template<class Engine>
void test_flat_deadline()
{
    Engine engine(std::make_unique<sink_io>(), 42, 100000);
    engine.set_position("startpos", "");
    const auto start = clock_type::now();
    auto best = engine.search(std::nullopt, 20, {});
    assert(best && engine.get_current_state()->can_apply(*best));
    assert(clock_type::now() - start < 1s);
    // An expired budget still permits a discovered legal fallback.
    best = engine.search(std::nullopt, 0, {});
    assert(best && engine.get_current_state()->can_apply(*best));
}

void test_snapshot_roundtrip()
{
    for(const auto layout : {timeline_layout::odd, timeline_layout::even})
    {
        const int left = layout.initial_range().first;
        std::vector<boards_info_t> boards{
            {left, 1, false, "k3/4/4/3K"}, {left, 1, true, "k3/4/3K/4"}};
        if(layout.is_even()) boards.emplace_back(0, 1, true, "k3/4/3K/4");
        state position(multiverse(layout, std::move(boards), 4, 4), promotion_options::KNIGHT);
        const state replayed = state::from_pgn(search_trace_position(position));
        assert(replayed.get_boards() == position.get_boards());
        assert(replayed.get_present() == position.get_present());
        assert(replayed.get_board_size() == position.get_board_size());
        assert(replayed.get_initial_lines_range() == position.get_initial_lines_range());
        assert(replayed.get_promotion_options() == position.get_promotion_options());
    }
    state initial(create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));
    assert(state::from_pgn(search_trace_position(initial)).show_fen() == initial.show_fen());
}
// Environment changes are isolated to this single-threaded diagnostic test.
struct scoped_environment
{
    std::string key;
    std::optional<std::string> old;
    static void assign(const std::string &key, const char *value)
    {
#ifdef _WIN32
        _putenv_s(key.c_str(), value ? value : "");
#else
        if(value) setenv(key.c_str(), value, 1);
        else unsetenv(key.c_str());
#endif
    }
    scoped_environment(std::string name, const char *value) : key(std::move(name))
    {
        if(const char *before = std::getenv(key.c_str())) old = before;
        assign(key, value);
    }
    ~scoped_environment() { assign(key, old ? old->c_str() : nullptr); }
};

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void test_slow_position_capture()
{
    const auto folder = std::filesystem::temp_directory_path()
        / ("5dchess-trace-test-" + std::to_string(clock_type::now().time_since_epoch().count()));
    const auto folder_string = folder.string();
    scoped_environment path("FIVEDCHESS_SEARCH_TRACE_DIR", folder_string.c_str());
    scoped_environment threshold("FIVEDCHESS_SEARCH_TRACE_MS", "0");
    state root(create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));
    state leaf = state::from_pgn("[Board \"Standard - Turn Zero\"]\n1. e4");
    std::stop_source stopped;
    stopped.request_stop();
    {
        search_trace trace(root, stopped.get_token(), clock_type::now() - 1ms,
                           "engine_command=test --seed 42");
        trace.set_iteration(36);
        {
            search_trace_phase phase("evaluation", &leaf);
        }
        assert(!std::filesystem::exists(folder)); // Output is deferred until completion.
    }
    assert(!search_trace::current);
    const auto directory = std::filesystem::directory_iterator(folder)->path();
    const auto details = read_file(directory / "0.txt");
    assert(details.find("phase=evaluation") != std::string::npos);
    assert(details.find("iteration=36") != std::string::npos);
    assert(details.find("stop_requested=1") != std::string::npos);
    assert(details.find("deadline_overshoot_ms=") != std::string::npos);
    assert(details.find("--seed 42") != std::string::npos);
    assert(state::from_pgn(read_file(directory / "0.5dpgn")).show_fen() == leaf.show_fen());
    assert(state::from_pgn(read_file(directory / "root.5dpgn")).show_fen() == root.show_fen());
    std::filesystem::remove_all(folder);

    scoped_environment disabled("FIVEDCHESS_SEARCH_TRACE_DIR", "");
    search_trace trace(root, {}, {}, "disabled");
    search_trace_phase phase("disabled", &root);
    assert(!trace.enabled() && !phase.enabled());
}

void test_zero_terminal_rewards()
{
    for(bool checkmate : {false, true})
    {
        state position(multiverse(timeline_layout::odd, {{0, 1, true, checkmate
            ? "k7/1Q6/2K5/8/8/8/8/8" : "k7/2Q5/2K5/8/8/8/8/8"}}));
        const auto result = evaluate_zero_position(position, {});
        assert(result);
        assert(result->data.end == (checkmate ? rollout_details::termination::WHITE_WINS
                                             : rollout_details::termination::STALEMATE));
        assert((position.get_mate_type() == mate_type::CHECKMATE) == checkmate);
    }
}
} // namespace

int main()
{
    test_cancellation_scope();
    test_deadline_inside_evaluation(false);
    test_deadline_inside_evaluation(true);
    test_flat_deadline<flat_ucb_engine>();
    test_flat_deadline<flat_ucb_weighted_engine>();
    test_snapshot_roundtrip();
    test_slow_position_capture();
    test_zero_terminal_rewards();
}
