#include <charconv>
#include <cstdlib>
#include "uci.h"
#include <pgnparser.h>
#include <sstream>

bool engine::is_busy() const
{
    return active_task.load() != task_state::idle;
}

void engine::launch_async_task(task_state task, std::function<void(std::stop_token)> work)
{
    if(search_thread.joinable())
    {
        search_thread.join();
    }

    active_task.store(task);
    search_thread = std::jthread([this, work = std::move(work)](std::stop_token st) mutable {
        try
        {
            work(st);
        }
        catch(...)
        {
            write_line("info engine task failed");
        }

        active_task.store(task_state::idle);
        task_cv.notify_all();
    });
}

engine::~engine()
{
    quit_requested = true;
    ready_pending = false;
    task_cv.notify_all();

    if(ready_thread.joinable())
    {
        ready_thread.join();
    }
}

void engine::stop_search()
{
    if(search_thread.joinable())
    {
        search_thread.request_stop();
    }
}

void engine::write_line(const std::string &line)
{
    std::lock_guard<std::mutex> lock(io_mutex);
    io->write_line(line);
}

void engine::mainloop()
{
    while(io->is_open() && !quit_requested.load())
    {
        if(search_thread.joinable() && !is_busy())
        {
            search_thread.join();
        }

        if(ready_thread.joinable() && !ready_pending.load())
        {
            ready_thread.join();
        }

        std::string line = io->read_line();
        if(line.empty())
        {
            if(search_thread.joinable() && !is_busy())
            {
                search_thread.join();
            }
            if(ready_thread.joinable() && !ready_pending.load())
            {
                ready_thread.join();
            }
            continue;
        }
        std::istringstream iss(line);
        std::string command;
        iss >> command;
        if(command == "5duci")
        {
            if(is_busy())
            {
                write_line("info engine is busy");
            }
            else
            {
                launch_async_task(task_state::initializing, [this](std::stop_token) {
                    initialize();
                    if(!quit_requested.load())
                    {
                        write_line("5duciok");
                    }
                });
            }
        }
        else if(command == "5ducinewgame")
        {
            start_new_game();
        }
        else if(command == "position")
        {
            std::string position;
            std::string moves;
            std::string token;
            bool reading_moves = false;
            while(iss >> token)
            {
                if(token == "move" || token == "moves")
                {
                    reading_moves = true;
                    continue;
                }

                if(reading_moves)
                {
                    if(!moves.empty())
                    {
                        moves += ' ';
                    }
                    moves += token;
                }
                else
                {
                    if(!position.empty())
                    {
                        position += ' ';
                    }
                    position += token;
                }
            }
            set_position(position, moves);
        }
        else if(command == "go")
        {
            int time_limit_ms = 1000; // default 1 second
            int depth_limit = 20; // default 20 search depth
            std::string token;
            while(iss >> token)
            {
                if(token == "movetime")
                {
                    iss >> time_limit_ms;
                }
                else if(token == "depth")
                {
                    iss >> depth_limit;
                }
                // can add more time control options here
            }
            if(is_busy())
            {
                write_line("info search already running");
            }
            else
            {
                launch_async_task(task_state::searching, [this, depth_limit, time_limit_ms](std::stop_token st) {
                    auto best_move = find_best_move(std::optional<int>(depth_limit), std::optional<int>(time_limit_ms), st);
                    if(quit_requested.load())
                    {
                        return;
                    }
                    if(best_move)
                    {
                        std::string best_move_str = "bestmove ";
                        for(const auto &ext_mv : best_move->get_moves())
                        {
                            best_move_str += ext_mv.to_string() + " ";
                        }
                        write_line(best_move_str);
                    }
                    else
                    {
                        write_line("nobestmove");
                    }
                });
            }
        }
        else if(command == "isready")
        {
            if(!is_busy())
            {
                write_line("readyok");
            }
            else if(!ready_pending.exchange(true))
            {
                if(ready_thread.joinable())
                {
                    ready_thread.join();
                }

                ready_thread = std::thread([this]() {
                    std::unique_lock<std::mutex> lock(task_mutex);
                    task_cv.wait(lock, [this]() {
                        return !is_busy() || quit_requested.load();
                    });
                    lock.unlock();
                    if(quit_requested.load())
                    {
                        ready_pending = false;
                        return;
                    }
                    write_line("readyok");
                    ready_pending = false;
                });
            }
        }
        else if(command == "quit")
        {
            quit_requested = true;
            ready_pending = false;
            task_cv.notify_all();
            if(is_busy())
            {
                stop_search();
            }
            write_line("bye");
            break;
        }
        else if(command == "stop")
        {
            stop_search();
        }
        else if(command == "setoption")
        {
            std::string token;
            if(iss >> token && token == "name")
            {
                // Build the key: read tokens until "value" is found
                std::string key;
                bool has_value = false;
                std::string value;
                while(iss >> token)
                {
                    if(token == "value")
                    {
                        has_value = true;
                        break;
                    }
                    if(!key.empty())
                    {
                        key += ' ';
                    }
                    key += token;
                }
                if(has_value)
                {
                    // Read the rest of the line as the value
                    std::string rest;
                    std::getline(iss, rest);
                    // Trim leading whitespace
                    auto first = rest.find_first_not_of(" \t");
                    if(first != std::string::npos)
                    {
                        value = rest.substr(first);
                    }
                    set_option(key, parse_option_value(value));
                }
                else
                {
                    set_option(key, option_value_t{std::monostate()});
                }
            }
        }

        if(search_thread.joinable() && !is_busy())
        {
            search_thread.join();
        }

        if(ready_thread.joinable() && !ready_pending.load())
        {
            ready_thread.join();
        }
    }

    ready_pending = false;
    task_cv.notify_all();

    if(ready_thread.joinable())
    {
        ready_thread.join();
    }

    if(search_thread.joinable())
    {
        search_thread.join();
    }
}

void engine::set_position(const std::string &position, const std::string &moves)
{

	if(position == "startpos")
	{
		s = get_startpos();
	}
    else
    {
        // parse this line:
        // [size <m>x<n>] [odd|even] fen <5dfen-string>
        int m = 0;
        int n = 0;
        std::string fen;
        bool is_even = false; // default false

        const std::string size_prefix = "size ";
        const std::string fen_prefix = " fen ";

        if(position.rfind(size_prefix, 0) == 0)
        {
            const auto fen_pos = position.find(fen_prefix);
            if(fen_pos == std::string::npos)
            {
                // fallback: treat whole tail as fen
                fen = position.substr(size_prefix.size());
            }
            else
            {
                std::string size_part = position.substr(size_prefix.size(), fen_pos - size_prefix.size());
                std::istringstream iss(size_part);
                std::string size_spec;
                iss >> size_spec; // expected like "4x5"
                auto x_pos = size_spec.find('x');
                if(x_pos != std::string::npos)
                {
                    m = std::stoi(size_spec.substr(0, x_pos));
                    n = std::stoi(size_spec.substr(x_pos + 1));
                }
                std::string parity_token;
                if(iss >> parity_token)
                {
                    if(parity_token == "even")
                        is_even = true;
                    // if "odd" or anything else, is_even stays false
                }
                fen = position.substr(fen_pos + fen_prefix.size());
            }
        }
        else
        {
            fen = position;
        }

        // build a variant_setup_t from parsed values and create state
        variant_setup_t vs;
        vs.size_x = m > 0 ? m : 8;
        vs.size_y = n > 0 ? n : 8;
        vs.boards.clear();

        // fen may contain multiple bracketed metadata blocks like: [fen:l:t:c][fen2:l:t:c]
        std::string rest = fen;
        size_t pos = 0;
        while(pos < rest.size())
        {
            // skip whitespace
            while(pos < rest.size() && isspace(static_cast<unsigned char>(rest[pos]))) pos++;
            if(pos >= rest.size()) break;
            if(rest[pos] == '[')
            {
                auto end = rest.find(']', pos);
                if(end == std::string::npos)
                {
                    // malformed; treat rest as a single metadata block without brackets
                    std::string inner = rest.substr(pos);
                    auto tup = pgnparser::parse_board_fen_metadata(inner);
                    vs.boards.push_back(tup);
                    break;
                }
                std::string inner = rest.substr(pos + 1, end - pos - 1);
                auto tup = pgnparser::parse_board_fen_metadata(inner);
                vs.boards.push_back(tup);
                pos = end + 1;
            }
            else
            {
                // no brackets: assume remainder is a single metadata entry
                std::string inner = rest.substr(pos);
                auto tup = pgnparser::parse_board_fen_metadata(inner);
                vs.boards.push_back(tup);
                break;
            }
        }

        vs.is_even_timelines = is_even;

        auto mv = create_multiverse_from_variant_setup(vs);
        s = state(*mv);
    }

    // apply moves if any
    std::istringstream iss(moves);
    std::string move_str;
    while(iss >> move_str)
    {
        if(move_str == "submit")
        {
            s->submit();
        }
        else
        {
            s->apply_move(full_move{move_str});
        }
    }
}

engine::option_value_t engine::get_option(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(options_mutex);
    auto it = options.find(key);
    return it != options.end() ? it->second : option_value_t{};
}

engine::option_value_t engine::parse_option_value(const std::string &value)
{
    // Try bool
    if(value == "true")
    {
        return option_value_t(true);
    }
    if(value == "false")
    {
        return option_value_t(false);
    }

    // 2. Try int via std::from_chars (no exceptions)
    int int_val = 0;
    auto [ptr_int, ec_int] = std::from_chars(value.data(), value.data() + value.size(), int_val);
    if(ec_int == std::errc() && ptr_int == value.data() + value.size())
    {
        return option_value_t(int_val);
    }

    // 3. Try double via std::strtod (no exceptions)
    char *end_dbl = nullptr;
    double double_val = std::strtod(value.data(), &end_dbl);
    if(end_dbl == value.data() + value.size() && value.size() > 0)
    {
        return option_value_t(double_val);
    }

    // 4. Fallback: string
    return option_value_t(value);
}

void engine::set_option(const std::string &key, const option_value_t &value)
{
    {
        std::lock_guard<std::mutex> lock(options_mutex);
        options[key] = value;
    }
    on_option_changed(key, value);
}

void engine::send_info(const std::string &info)
{
    std::lock_guard<std::mutex> lock(io_mutex);
    io->write_line("info " + info);
}

void engine::on_option_changed(const std::string & /*key*/, const option_value_t & /*value*/)
{
    // Default no-op — derived engines override to react to specific options.
}