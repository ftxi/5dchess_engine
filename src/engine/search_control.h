#ifndef SEARCH_CONTROL_H
#define SEARCH_CONTROL_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

// One cancellation state per search. No timer thread is needed for untimed work.
// Members are ordered so the timer joins before its wait state is destroyed.
class search_stop_scope
{
public:
    using clock = std::chrono::steady_clock;

private:
    std::stop_source source;
    struct forward_stop
    {
        std::stop_source source;
        void operator()() const noexcept { source.request_stop(); }
    };
    std::stop_callback<forward_stop> external_callback;
    std::optional<clock::time_point> deadline;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::jthread timer;

public:
    search_stop_scope(std::stop_token external,
                      std::optional<clock::time_point> deadline_value)
        : external_callback(external, forward_stop{source}), deadline(deadline_value)
    {
        if(!deadline || source.stop_requested()) return;
        if(clock::now() >= *deadline)
        {
            source.request_stop();
            return;
        }
        timer = std::jthread([this](std::stop_token shutdown) {
            // External cancellation also wakes the timer; early completion is
            // handled by jthread's stop request on destruction.
            std::stop_callback cancelled(source.get_token(), [&] { timer_wake(); });
            std::unique_lock lock(mutex);
            wake.wait_until(lock, shutdown, *deadline,
                            [&] { return source.stop_requested(); });
            const bool expired = !shutdown.stop_requested()
                && !source.stop_requested() && clock::now() >= *deadline;
            lock.unlock();
            if(expired) source.request_stop();
        });
    }

    std::stop_token get_token() const noexcept { return source.get_token(); }
    bool deadline_expired() const noexcept
    {
        return deadline && clock::now() >= *deadline;
    }

private:
    void timer_wake() noexcept
    {
        // Locking pairs with the predicate check, avoiding a lost notification.
        std::lock_guard lock(mutex);
        wake.notify_all();
    }
};

#endif
