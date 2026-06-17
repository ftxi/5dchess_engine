#ifndef UCI_H
#define UCI_H

#include <memory>
#include <optional>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include "state.h"
#include "variants.h"
#include "action.h"
#include "io_handler.h"

/*
 * 5DUCI (Universal 5D Chess Interface) engine base class
 * This class defines the interface for a 5D chess engine that can be used with the 5DUCI protocol.
 * An engine must implement the following methods:
 * - initialize(): called once received "5duci" command. When this methods halts, the base class will send "5duciok" to the GUI.
 * - find_best_move(): called when the engine received "go" command. Return the best action found within the given limits. The base class will send "bestmove <move>" to the GUI after this method returns an action, or "nobestmove" if it returns std::nullopt.
 * - stop_search(): called when the engine should stop searching for the best move. The derived class should be able to stop searching immediately and output the best move found so far.
 */

class engine
{
public:
    enum class task_state
    {
        idle,
        initializing,
        searching
    };

private:
    std::optional<state> s;
    std::unique_ptr<io_handler> io; 
    std::atomic<task_state> active_task{task_state::idle};
    std::thread search_thread;
    std::thread ready_thread;
    std::atomic<bool> ready_pending{false};
    std::atomic<bool> quit_requested{false};
    std::condition_variable task_cv;
    std::mutex task_mutex;
    std::mutex io_mutex;
protected:
    static const state& get_startpos()
    {
        static const state value{*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero"))};
        return value;
    }
    void write_line(const std::string &line);
    bool is_busy() const;
    // Starts an asynchronous engine task and marks the engine busy until the callback finishes.
    // Use this for work that may block, such as initialize() or find_best_move().
    void launch_async_task(task_state task, std::function<void()> work);
public:
    engine(std::unique_ptr<io_handler> io_handler) : s(std::nullopt), io(std::move(io_handler)) {}
    virtual void initialize() = 0;
    void mainloop();
    const std::optional<state> &get_current_state() const
    {
        return s;
    }
    virtual void start_new_game()
    {
        s = std::nullopt;
    }
    void set_position(const std::string &position, const std::string &moves);
    virtual std::optional<action> find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms) = 0;
    virtual void stop_search() = 0;
    virtual ~engine();
};

#endif /* UCI_H*/