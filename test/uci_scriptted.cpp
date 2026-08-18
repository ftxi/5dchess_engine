#include <atomic>
#undef NDEBUG // make sure assert is always enabled
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <map>

#include "engine/uci.h"
#include <stop_token>
#include <chrono>

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
    struct option_change
    {
        std::string key;
        option_value_t value;
    };

    explicit dummy_engine(std::unique_ptr<io_handler> io) : engine(std::move(io)) {}

    void initialize() override
    {
    }

    std::optional<action> find_best_move(std::optional<int>, std::optional<int>, std::stop_token stop_token) override
    {
        std::unique_lock<std::mutex> lock(search_mutex);
        while(!stop_token.stop_requested())
        {
            search_cv.wait_for(lock, std::chrono::milliseconds(10));
        }

        const auto &current_state = get_current_state();
        if(!current_state.has_value())
        {
            return std::nullopt;
        }

        std::vector<ext_move> moves;
        moves.emplace_back(full_move("(0T0)a1a1"));
        return action::from_vector(moves, current_state.value());
    }

    const std::vector<option_change>& option_changes() const
    {
        return changes;
    }

protected:
    void on_option_changed(const std::string &key, const option_value_t &value) override
    {
        changes.push_back({key, value});
    }

private:
    std::mutex search_mutex;
    std::condition_variable search_cv;
    std::vector<option_change> changes;
};

int main()
{
    auto io = std::make_unique<scripted_io_handler>(std::vector<scripted_io_handler::scripted_line>{
        {"5duci", 0},
        {"setoption name UCI_AnalyseMode value true", 1},
        {"setoption name MultiPV value 4", 1},
        {"setoption name UCI_LimitStrength value 90.5", 1},
        {"setoption name Clear Hash", 1},
        {"setoption name UCI_EngineName value MyEngine", 1},
        {"5ducinewgame", 1},
        {"position size 3x3 even fen [k*2/3/2K*:-0:1:w] [k*2/3/2K*:+0:1:w] move (0T1)c1b1 (-1T1)c1b1 submit", 1},
        {"print", 2},
        {"go depth 1 time 1", 1},
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

    std::cout << "\non_option_changed invocations:\n";
    for(const auto &ch : eng.option_changes())
    {
        std::cout << "  " << ch.key << " -> ";
        if(std::holds_alternative<bool>(ch.value))
            std::cout << (std::get<bool>(ch.value) ? "true" : "false") << " (bool)";
        else if(std::holds_alternative<int>(ch.value))
            std::cout << std::get<int>(ch.value) << " (int)";
        else if(std::holds_alternative<double>(ch.value))
            std::cout << std::get<double>(ch.value) << " (double)";
        else if(std::holds_alternative<std::string>(ch.value))
            std::cout << '"' << std::get<std::string>(ch.value) << "\" (string)";
        else
            std::cout << "(monostate)";
        std::cout << '\n';
    }

    // Verify basic UCI protocol
    assert(io_ptr->output_lines.size() >= 2);
    assert(io_ptr->output_lines.front() == "5duciok");
    //assert(io_ptr->output_lines[1].rfind("bestmove ", 0) == 0);

    // Verify setoption: options stored correctly
    auto analyse = eng.get_option("UCI_AnalyseMode");
    assert(std::holds_alternative<bool>(analyse));
    assert(std::get<bool>(analyse) == true);

    auto multipv = eng.get_option("MultiPV");
    assert(std::holds_alternative<int>(multipv));
    assert(std::get<int>(multipv) == 4);

    auto strength = eng.get_option("UCI_LimitStrength");
    assert(std::holds_alternative<double>(strength));
    assert(std::get<double>(strength) == 90.5);

    auto clear = eng.get_option("Clear Hash");
    assert(std::holds_alternative<std::monostate>(clear));

    auto name = eng.get_option("UCI_EngineName");
    assert(std::holds_alternative<std::string>(name));
    assert(std::get<std::string>(name) == "MyEngine");

    // Verify on_option_changed was called for each setoption
    const auto &changes = eng.option_changes();
    assert(changes.size() == 5);
    assert(changes[0].key == "UCI_AnalyseMode");
    assert(std::holds_alternative<bool>(changes[0].value));
    assert(changes[3].key == "Clear Hash");
    assert(std::holds_alternative<std::monostate>(changes[3].value));

    // Position moves retain the promotion piece encoded by ext_move.
    auto promotion_io = std::make_unique<scripted_io_handler>(
        std::vector<scripted_io_handler::scripted_line>{});
    dummy_engine promotion_eng(std::move(promotion_io));
    promotion_eng.set_position(
        "size 4x4 odd fen [3k/P3/4/K*3:0:1:w]",
        "(0T1)a3a4N");
    assert(promotion_eng.get_current_state()->get_piece(vec4(0, 3, 1, 0), true) == KNIGHT_W);

    // Invalid position histories report an error without terminating the
    // main loop. A later valid position command must still be accepted.
    auto recovery_io = std::make_unique<scripted_io_handler>(
        std::vector<scripted_io_handler::scripted_line>{
            {"position startpos moves (0T1)h2h5", 0},
            {"position startpos moves submit", 1},
            {"position startpos moves malformed", 2},
            {"position startpos moves (0T1)h2h4 submit", 3},
            {"print", 3},
            {"quit", 4}
        });
    auto *recovery_io_ptr = recovery_io.get();
    dummy_engine recovery_eng(std::move(recovery_io));
    recovery_eng.mainloop();

    assert(recovery_io_ptr->output_lines.size() == 5);
    assert(recovery_io_ptr->output_lines[0]
           == "info position error: cannot apply move (0T1)h2h5");
    assert(recovery_io_ptr->output_lines[1]
           == "info position error: cannot submit move history");
    assert(recovery_io_ptr->output_lines[2].starts_with(
        "info position error: cannot parse move malformed:"));
    assert(recovery_io_ptr->output_lines[3].find("7P") != std::string::npos);
    assert(recovery_io_ptr->output_lines[4] == "bye");

    std::cout << "All setoption tests passed!\n";
    return 0;
}
