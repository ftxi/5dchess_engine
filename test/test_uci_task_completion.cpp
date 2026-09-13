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
        return inputs[next_input++].text;
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

void terminal_response_is_published_after_idle()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"go movetime 1", 1},
            {"isready", 2},
            {"quit", 3},
        });
    auto *io_ptr = io.get();
    immediate_engine eng(std::move(io));
    bool terminal_was_idle = false;
    io_ptr->observer = [&eng, &terminal_was_idle](const std::string &line) {
        if(line == "nobestmove")
        {
            terminal_was_idle = !eng.busy_for_test();
        }
    };

    eng.mainloop();

    assert(terminal_was_idle);
    assert((io_ptr->output_lines() == std::vector<std::string>{
        "5duciok", "nobestmove", "readyok", "bye"}));
}

void position_is_rejected_during_search()
{
    auto io = std::make_unique<observing_io_handler>(
        std::vector<observing_io_handler::scripted_line>{
            {"5duci", 0},
            {"position startpos", 1},
            {"go movetime 1000", 1},
            {"position startpos moves (0T1)h2h4 submit", 1},
            {"stop", 2},
            {"isready", 2},
            {"quit", 4},
        });
    auto *io_ptr = io.get();
    stoppable_engine eng(std::move(io));

    eng.mainloop();

    assert((io_ptr->output_lines() == std::vector<std::string>{
        "5duciok",
        "info engine is busy, please run 'stop' first",
        "nobestmove",
        "readyok",
        "bye",
    }));
}

int main()
{
    terminal_response_is_published_after_idle();
    position_is_rejected_during_search();
    return 0;
}
