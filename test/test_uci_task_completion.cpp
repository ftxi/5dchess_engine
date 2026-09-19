#undef NDEBUG
#include <cassert>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "engine/uci.h"

class observing_io_handler : public io_handler
{
public:
    struct scripted_line
    {
        std::string text;
        std::size_t min_outputs_before_release;
    };

    explicit observing_io_handler(std::vector<scripted_line> inputs)
        : inputs(std::move(inputs))
    {}

    std::string read_line() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]() {
            return next_input >= inputs.size()
                || outputs.size() >= inputs[next_input].min_outputs_before_release;
        });
        if(next_input == inputs.size())
        {
            return {};
        }
        std::string line = inputs[next_input++].text;
        lock.unlock();
        if(input_observer)
        {
            input_observer(line);
        }
        return line;
    }

    void write_line(const std::string &line) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(observer)
        {
            observer(line);
        }
        outputs.push_back(line);
        cv.notify_all();
    }

    bool is_open() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return next_input < inputs.size();
    }

    std::vector<std::string> output_lines()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return outputs;
    }

    std::function<void(const std::string &)> observer;
    std::function<void(const std::string &)> input_observer;

private:
    std::vector<scripted_line> inputs;
    std::vector<std::string> outputs;
    std::size_t next_input = 0;
    std::mutex mutex;
    std::condition_variable cv;
};

class immediate_engine : public engine
{
public:
    explicit immediate_engine(std::unique_ptr<io_handler> io)
        : engine(std::move(io))
    {}

    void initialize() override {}

    std::optional<action> find_best_move(
        std::optional<int>, std::optional<int>, std::stop_token) override
    {
        return std::nullopt;
    }

    bool busy_for_test() const
    {
        return is_busy();
    }
};

class stoppable_engine : public immediate_engine
{
public:
    using immediate_engine::immediate_engine;

    std::optional<action> find_best_move(
        std::optional<int>, std::optional<int>, std::stop_token stop_token) override
    {
        while(!stop_token.stop_requested())
        {
            std::this_thread::yield();
        }
        return std::nullopt;
    }
};

class reporting_engine : public immediate_engine
{
public:
    using immediate_engine::immediate_engine;

    std::optional<action> find_best_move(
        std::optional<int>, std::optional<int>, std::stop_token) override
    {
        send_info("depth 1");
        send_info("depth 2");
        return std::nullopt;
    }
};

class controlled_setup_engine : public immediate_engine
{
public:
    enum class blocked_operation
    {
        initialize,
        new_game
    };

    controlled_setup_engine(
        std::unique_ptr<io_handler> io,
        blocked_operation operation)
        : immediate_engine(std::move(io)), operation(operation)
    {}

    void initialize() override
    {
        if(operation == blocked_operation::initialize)
        {
            wait_for_release();
        }
    }

    void start_new_game() override
    {
        if(operation == blocked_operation::new_game)
        {
            wait_for_release();
        }
        immediate_engine::start_new_game();
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        cv.notify_all();
    }

private:
    void wait_for_release()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]() { return released; });
    }

    blocked_operation operation;
    std::mutex mutex;
    std::condition_variable cv;
    bool released = false;
};

void terminal_response_is_published_after_idle()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"go movetime 1", 1},
            {"position startpos", 4},
            {"print", 4},
            {"quit", 5},
        });
    auto *io_ptr = io.get();
    reporting_engine eng(std::move(io));
    bool terminal_was_idle = false;
    io_ptr->observer = [&eng, &terminal_was_idle](const std::string &line) {
        if(line == "nobestmove")
        {
            terminal_was_idle = !eng.busy_for_test();
        }
    };

    eng.mainloop();

    assert(terminal_was_idle);
    const auto outputs = io_ptr->output_lines();
    assert(outputs.size() == 6);
    assert((std::vector<std::string>(outputs.begin(), outputs.begin() + 4)
        == std::vector<std::string>{
            "5duciok", "info depth 1", "info depth 2", "nobestmove"}));
    assert(outputs[4].starts_with("["));
    assert(outputs[5] == "bye");
}

void isready_is_immediate_during_search()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"position startpos", 1},
            {"go movetime 1000", 1},
            {"isready", 1},
            {"isready", 2},
            {"position startpos moves (0T1)h2h4 submit", 3},
            {"stop", 4},
            {"quit", 5},
        });
    auto *io_ptr = io.get();
    stoppable_engine eng(std::move(io));

    eng.mainloop();

    assert((io_ptr->output_lines() == std::vector<std::string>{
        "5duciok",
        "readyok",
        "readyok",
        "info engine is busy, please run 'stop' first",
        "nobestmove",
        "bye",
    }));
}

void initialization_defers_every_isready()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"isready", 0},
            {"isready", 0},
            {"stop", 0},
            {"quit", 3},
        });
    auto *io_ptr = io.get();
    controlled_setup_engine eng(
        std::move(io), controlled_setup_engine::blocked_operation::initialize);
    io_ptr->input_observer = [&eng](const std::string &line) {
        if(line == "stop")
        {
            eng.release();
        }
    };

    eng.mainloop();

    assert((io_ptr->output_lines() == std::vector<std::string>{
        "5duciok", "readyok", "readyok", "bye"}));
}

void new_game_defers_every_isready()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"5ducinewgame", 1},
            {"isready", 1},
            {"isready", 1},
            {"stop", 1},
            {"quit", 3},
        });
    auto *io_ptr = io.get();
    controlled_setup_engine eng(
        std::move(io), controlled_setup_engine::blocked_operation::new_game);
    io_ptr->input_observer = [&eng](const std::string &line) {
        if(line == "stop")
        {
            eng.release();
        }
    };

    eng.mainloop();

    assert((io_ptr->output_lines() == std::vector<std::string>{
        "5duciok", "readyok", "readyok", "bye"}));
}

void quit_discards_deferred_isready()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"isready", 0},
            {"quit", 0},
        });
    auto *io_ptr = io.get();
    controlled_setup_engine eng(
        std::move(io), controlled_setup_engine::blocked_operation::initialize);
    io_ptr->observer = [&eng](const std::string &line) {
        if(line == "bye")
        {
            eng.release();
        }
    };

    eng.mainloop();

    assert((io_ptr->output_lines() == std::vector<std::string>{"bye"}));
}

int main()
{
    terminal_response_is_published_after_idle();
    isready_is_immediate_during_search();
    initialization_defers_every_isready();
    new_game_defers_every_isready();
    quit_discards_deferred_isready();
    return 0;
}
