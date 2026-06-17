#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/uci.h"

class scripted_io_handler : public io_handler
{
public:
    struct scripted_line
    {
        std::string text;
        size_t min_outputs_before_release;
    };

    explicit scripted_io_handler(std::vector<scripted_line> input_lines) : input_lines(std::move(input_lines)) {}

    std::string read_line() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]() {
            return next_input >= input_lines.size() || output_lines.size() >= input_lines[next_input].min_outputs_before_release;
        });

        if(next_input < input_lines.size())
        {
            return input_lines[next_input++].text;
        }
        return {};
    }

    void write_line(const std::string &line) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        output_lines.push_back(line);
        cv.notify_all();
    }

    bool is_open() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return next_input < input_lines.size();
    }

    std::vector<std::string> output_lines;

private:
    std::vector<scripted_line> input_lines;
    size_t next_input = 0;
    std::mutex mutex;
    std::condition_variable cv;
};

class dummy_engine : public engine
{
public:
    explicit dummy_engine(std::unique_ptr<io_handler> io) : engine(std::move(io)) {}

    void initialize() override
    {
    }

    std::optional<action> find_best_move(std::optional<int>, std::optional<int>) override
    {
        std::unique_lock<std::mutex> lock(search_mutex);
        search_cv.wait(lock, [this]() {
            return stop_requested.load();
        });

        const auto &current_state = get_current_state();
        if(!current_state.has_value())
        {
            return std::nullopt;
        }

        std::vector<ext_move> moves;
        moves.emplace_back(full_move("(0T0)a1a1"));
        return action::from_vector(moves, current_state.value());
    }

    void stop_search() override
    {
        stop_requested = true;
        search_cv.notify_all();
    }

private:
    std::atomic<bool> stop_requested{false};
    std::mutex search_mutex;
    std::condition_variable search_cv;
};

int main()
{
    auto io = std::make_unique<scripted_io_handler>(std::vector<scripted_io_handler::scripted_line>{
        {"5duci", 0},
        {"5ducinewgame", 1},
        {"position size 3x3 even fen [k*2/3/2K*:-0:1:w] [k*2/3/2K*:+0:1:w] move (0T1)c1b1 (-1T1)c1b1 submit", 1},
        {"go depth 1 movetime 1", 1},
        {"isready", 1},
        {"stop", 1},
        {"isready", 2},
        {"quit", 2}
    });
    auto *io_ptr = io.get();
    dummy_engine eng(std::move(io));
    eng.mainloop();

    std::cout << "Transcript:\n";
    for(const auto &line : io_ptr->output_lines)
    {
        std::cout << line << '\n';
    }

    assert(io_ptr->output_lines.size() >= 2);
    assert(io_ptr->output_lines.front() == "5duciok");
    assert(io_ptr->output_lines[1].rfind("bestmove ", 0) == 0);
    return 0;
}
