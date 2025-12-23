#include "game.h"
#include <regex>
#include <iostream>
#include <ranges>
#include <algorithm>
#include <cassert>
#include <variant>
#include <thread>
#include <future>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include "pgnparser.h"
#include "hypercuboid.h"


std::string show_comments(std::vector<std::string> const& comments)
{
    std::string result;
    bool first = true;
    for (auto const& c : comments)
    {
        if (!first) result += ' ';
        result += '{' + c + '}';
        first = false;
    }
    return result;
}

game::game(std::unique_ptr<gnode<comments_t>> gt)
: gametree{std::move(gt)}, current_node{gametree.get()}, cached{}
{
    cached.push_back(std::make_pair(current_node->get_state(), std::nullopt));
    now = cached.begin();
}

game game::from_pgn(std::string input)
{
    auto ag = pgnparser(input).parse_game();
    if(!ag.has_value())
        throw std::runtime_error("Bad input, parse failed");
    pgnparser_ast::gametree gt_ast = std::move(ag->gt);
    ag->gt = {};
    game g(gnode<comments_t>::create_root(state(*ag), comments_t{}));
    g.metadata = ag->headers;
    gnode<comments_t> *cn = nullptr;
    // parse moves
    std::function<void(gnode<comments_t>*, const pgnparser_ast::gametree&)> dfs;
    dfs = [&dfs, &cn](gnode<comments_t>* node, const pgnparser_ast::gametree& gt_ast) -> void {
        for(const auto& [act_ast, child_gt] : gt_ast.variations)
        {
            state s = node->get_state();
            std::vector<ext_move> moves;
            for(const auto& mv_ast: act_ast.moves)
            {
                auto [fm_opt, pt_opt, candidates] = s.parse_move(mv_ast);
                if(!fm_opt.has_value())
                {
                    if(candidates.empty())
                    {
                        std::ostringstream oss;
                        oss << "state(): Invalid move: " << mv_ast;
                        
                        throw std::runtime_error(oss.str());
                    }
                    else
                    {
                        std::ostringstream oss;
                        oss << "state(): Ambiguous move: " << mv_ast << "; candidates: ";
                        oss << range_to_string(candidates, "", "");
                        throw std::runtime_error(oss.str());
                    }
                }
                else
                {
                    full_move fm = fm_opt.value();
                    bool flag;
                    if(pt_opt.has_value())
                    {
                        flag = s.apply_move<false>(fm, *pt_opt);
                    }
                    else
                    {
                        flag = s.apply_move<false>(fm);
                    }
                    if(!flag)
                    {
                        std::ostringstream oss;
                        oss << "state(): Illegal move: " << mv_ast << " (parsed as: " << fm << ")";
                        throw std::runtime_error(oss.str());
                    }
                    moves.push_back(ext_move(fm, pt_opt.has_value() ? *pt_opt : QUEEN_W));
                }
            }
            bool flag = s.submit();
            if(!flag)
            {
                std::ostringstream oss;
                oss << "state(): Cannot submit after parsing these moves: " << act_ast;
                throw std::runtime_error(oss.str());
            }
            action act = action::from_vector(moves, node->get_state());
            std::unique_ptr<gnode<comments_t>> child_node = gnode<comments_t>::create_child(node, s, act, act_ast.comments);
            gnode<comments_t>* child_node_ptr = node->add_child(std::move(child_node));
            cn = child_node_ptr;
            dfs(child_node_ptr, *child_gt);
        }
    };
    
    dfs(g.gametree.get(), gt_ast);
    if(cn)
        g.current_node = cn;
    g.fresh();
    return g;
}

void game::fresh()
{
    cached.clear();
    cached.push_back(std::make_pair(current_node->get_state(), std::nullopt));
    now = cached.begin();
}

std::pair<int,bool> game::get_current_present() const
{
    return get_current_state().get_present();
}

state game::get_current_state() const
{
    return now->first;
}

std::vector<std::tuple<int, int, bool, std::string>> game::get_current_boards() const
{
    return get_current_state().get_boards();
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> game::get_current_timeline_status() const
{
    return get_current_state().get_timeline_status();
}

std::vector<vec4> game::gen_move_if_playable(vec4 p)
{
    if(is_playable(p))
    {
        const state& cs = get_current_state();
        std::vector<vec4> result;
        for(vec4 v : cs.gen_piece_move(p))
        {
            result.push_back(v);
        }
        return result;
    }
    else
    {
        return std::vector<vec4>();
    }
}

match_status_t game::get_match_status() const
{
    const state &s = current_node->get_state();
    auto [w, ss] = HC_info::build_HC(s);
    if(w.search(ss).first().has_value())
    {
        return match_status_t::PLAYING;
    }
    auto [t, c] = s.get_present();
    if(s.phantom().find_checks(!c).first().has_value())
    {
        return c ? match_status_t::WHITE_WINS : match_status_t::BLACK_WINS;
    }
    else
    {
        return match_status_t::STALEMATE;
    }
}

std::vector<vec4> game::get_movable_pieces() const
{
    state s = get_current_state();
    return s.gen_movable_pieces();
}

bool game::is_playable(vec4 p) const
{
    auto [mandatory_timelines, optional_timelines, unplayable_timelines] = get_current_timeline_status();
    const state& cs = get_current_state();
    if (std::find(mandatory_timelines.begin(), mandatory_timelines.end(), p.l()) != mandatory_timelines.end() ||
        std::find(optional_timelines.begin(), optional_timelines.end(), p.l()) != optional_timelines.end()) 
    {
        auto [t, c] = cs.get_present();
        auto v1 = std::make_pair(p.t(), c);
        auto v2 = cs.get_timeline_end(p.l());
        if(v1 == v2)
        {
            piece_t p_piece = cs.get_piece(p, c);
            if(p_piece != NO_PIECE && p_piece != WALL_PIECE)
            {
                return c == static_cast<int>(piece_color(p_piece));
            }
        }
    }
    return false;
}

bool game::can_undo() const
{
    return now != cached.begin();
}

bool game::can_redo() const
{
    return now+1 != cached.end();
}

bool game::can_submit() const
{
    return get_current_state().can_submit().has_value();
}

bool game::undo()
{
    bool flag = can_undo();
    if(flag)
    {
        now--;
    }
    return flag;
}

bool game::redo()
{
    bool flag = can_redo();
    if(flag)
    {
        now++;
    }
    return flag;
}

bool game::apply_move(ext_move m)
{
    std::optional<state> ans = now->first.can_apply(m.fm, m.promote_to);
    if(ans)
    {
        state new_state = std::move(ans.value());
        cached.erase(now + 1, cached.end());
        cached.push_back(std::make_pair(new_state, std::make_optional(m)));
        now = cached.end() - 1;
        return true;
    }
    return false;
}

bool game::submit()
{
    std::optional<state> ans = now->first.can_submit();
    if(ans)
    {
        std::vector<ext_move> mvs;
        for(const auto &[s,m] : cached)
        {
            if(m)
            {
                mvs.push_back(*m);
            }
        }
        visit_child(action::from_vector(mvs, cached.begin()->first));
        return true;
    }
    return false;
}

bool game::currently_check() const
{
    auto [t, c] = get_current_state().get_present();
    return get_current_state().find_checks(!c).first().has_value();
}

std::vector<std::pair<vec4, vec4>> game::get_current_checks() const
{
    auto [t, c] = get_current_state().get_present();
    std::vector<std::pair<vec4, vec4>> result;
    for(full_move fm : get_current_state().find_checks(!c))
    {
        result.push_back(std::make_pair(fm.from, fm.to));
    }
    return result;
}

std::pair<int, int> game::get_board_size() const
{
    return get_current_state().get_board_size();
}

bool game::suggest_action()
{
    const state &s = current_node->get_state();
    auto [w, ss] = HC_info::build_HC(s);
    for(moveseq mvs : w.search(ss))
    {
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
            return ext_move(m);
        });
        action act = action::from_vector(emvs, s);
        if(!current_node->find_child(act))
        {
            visit_child(act);
            visit_parent();
            return true;
        }
    }
    return false;
}

uint64_t game::count_actions() const
{
    const state &s = current_node->get_state();
    // Check if game is over
    auto ms = s.can_submit();
    if(ms.has_value())
    {
        // Current player can submit without making moves (game continues)
        // This means we have exactly 1 action: submit
        return 1;
    }
    
    auto [w, ss] = HC_info::build_HC(s);
    uint64_t count = 0;
    for([[maybe_unused]] moveseq mvs : w.search(ss))
    {
        ++count;
    }
    return count;
}

// Helper function for recursive perft
static uint64_t perft_recursive(const state &s, int depth, uint64_t &progress, uint64_t total, 
                                std::function<void(uint64_t, uint64_t)> callback)
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Check if current player can submit (meaning the turn ends without additional moves)
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        // Player must submit, which transitions to next player's turn
        if(depth == 1)
        {
            return 1;
        }
        return perft_recursive(*submit_state, depth - 1, progress, total, callback);
    }
    
    auto [w, ss] = HC_info::build_HC(s);
    uint64_t count = 0;
    
    for(moveseq mvs : w.search(ss))
    {
        if(depth == 1)
        {
            ++count;
        }
        else
        {
            // Apply action to get new state
            std::vector<ext_move> emvs;
            std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
                return ext_move(m);
            });
            action act = action::from_vector(emvs, s);
            auto new_state_opt = s.can_apply(act);
            if(new_state_opt.has_value())
            {
                // Check if this new state requires a submit
                auto submitted = new_state_opt->can_submit();
                if(submitted.has_value())
                {
                    count += perft_recursive(*submitted, depth - 1, progress, total, callback);
                }
                else
                {
                    count += perft_recursive(*new_state_opt, depth - 1, progress, total, callback);
                }
            }
        }
        
        // Report progress for depth 1 enumeration at root
        if(callback && depth > 1)
        {
            ++progress;
            callback(progress, total);
        }
    }
    
    return count;
}

uint64_t game::perft(int depth, std::function<void(uint64_t, uint64_t)> callback) const
{
    if(depth <= 0)
    {
        return 1;
    }
    
    const state &s = current_node->get_state();
    
    // First, count total actions at depth 1 for progress tracking
    uint64_t total = 0;
    if(callback && depth > 1)
    {
        total = const_cast<game*>(this)->count_actions();
    }
    
    uint64_t progress = 0;
    return perft_recursive(s, depth, progress, total, callback);
}

// Single-threaded perft worker (no callback, for use in threads)
static uint64_t perft_worker(const state &s, int depth)
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Check if current player can submit
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        if(depth == 1)
        {
            return 1;
        }
        return perft_worker(*submit_state, depth - 1);
    }
    
    auto [w, ss] = HC_info::build_HC(s);
    uint64_t count = 0;
    
    for(moveseq mvs : w.search(ss))
    {
        if(depth == 1)
        {
            ++count;
        }
        else
        {
            std::vector<ext_move> emvs;
            std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
                return ext_move(m);
            });
            action act = action::from_vector(emvs, s);
            auto new_state_opt = s.can_apply(act);
            if(new_state_opt.has_value())
            {
                auto submitted = new_state_opt->can_submit();
                if(submitted.has_value())
                {
                    count += perft_worker(*submitted, depth - 1);
                }
                else
                {
                    count += perft_worker(*new_state_opt, depth - 1);
                }
            }
        }
    }
    
    return count;
}

// Task structure for work stealing
struct PerftTask {
    state s;
    int depth;
};

uint64_t game::perft_parallel(int depth, unsigned int num_threads) const
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Auto-detect number of threads if not specified
    if(num_threads == 0)
    {
        num_threads = std::thread::hardware_concurrency();
        if(num_threads == 0) num_threads = 4; // fallback
    }
    
    const state &s = current_node->get_state();
    
    // For depth 1, just count directly (no benefit from parallelism)
    if(depth == 1)
    {
        return count_actions();
    }
    
    // Check if current player can submit
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        // Create a temporary game-like context for recursive call
        return perft_worker(*submit_state, depth - 1);
    }
    
    // Collect all first-level actions to distribute among threads
    std::vector<PerftTask> tasks;
    auto [w, ss] = HC_info::build_HC(s);
    
    for(moveseq mvs : w.search(ss))
    {
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
            return ext_move(m);
        });
        action act = action::from_vector(emvs, s);
        auto new_state_opt = s.can_apply(act);
        if(new_state_opt.has_value())
        {
            auto submitted = new_state_opt->can_submit();
            if(submitted.has_value())
            {
                tasks.push_back({*submitted, depth - 1});
            }
            else
            {
                tasks.push_back({*new_state_opt, depth - 1});
            }
        }
    }
    
    if(tasks.empty())
    {
        return 0;
    }
    
    // Use a thread pool approach with work stealing
    std::atomic<uint64_t> total_count{0};
    std::atomic<size_t> task_index{0};
    
    auto worker = [&]() {
        uint64_t local_count = 0;
        while(true)
        {
            size_t idx = task_index.fetch_add(1, std::memory_order_relaxed);
            if(idx >= tasks.size())
            {
                break;
            }
            local_count += perft_worker(tasks[idx].s, tasks[idx].depth);
        }
        total_count.fetch_add(local_count, std::memory_order_relaxed);
    };
    
    // Limit threads to task count
    unsigned int actual_threads = std::min(num_threads, static_cast<unsigned int>(tasks.size()));
    
    std::vector<std::thread> threads;
    threads.reserve(actual_threads);
    
    for(unsigned int i = 0; i < actual_threads; ++i)
    {
        threads.emplace_back(worker);
    }
    
    for(auto& t : threads)
    {
        t.join();
    }
    
    return total_count.load();
}

//////////////////////////////////////////////
// Perft with Transposition Table (TT)
//////////////////////////////////////////////

// Simple hash function for state - combines board hashes
static uint64_t compute_state_hash(const state& s)
{
    uint64_t hash = 0;
    auto boards = s.get_boards();
    
    // FNV-1a hash
    const uint64_t FNV_PRIME = 0x100000001b3;
    const uint64_t FNV_OFFSET = 0xcbf29ce484222325;
    
    hash = FNV_OFFSET;
    
    // Hash the present and player
    auto [present_t, player] = s.get_present();
    hash ^= static_cast<uint64_t>(present_t);
    hash *= FNV_PRIME;
    hash ^= static_cast<uint64_t>(player);
    hash *= FNV_PRIME;
    
    // Hash all boards
    for(const auto& [l, t, c, fen] : boards)
    {
        hash ^= static_cast<uint64_t>(l);
        hash *= FNV_PRIME;
        hash ^= static_cast<uint64_t>(t);
        hash *= FNV_PRIME;
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_PRIME;
        
        // Hash the FEN string
        for(char ch : fen)
        {
            hash ^= static_cast<uint64_t>(ch);
            hash *= FNV_PRIME;
        }
    }
    
    return hash;
}

// Transposition table entry
struct TTEntry {
    uint64_t hash;      // Full hash for verification
    uint64_t count;     // Perft count
    int depth;          // Depth at which this was computed
    bool valid;         // Entry validity flag
    
    TTEntry() : hash(0), count(0), depth(0), valid(false) {}
};

// Thread-local transposition table to avoid lock contention
class TranspositionTable {
    std::vector<TTEntry> table;
    size_t mask;
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    
public:
    TranspositionTable(size_t size_mb) {
        // Calculate number of entries
        size_t num_entries = (size_mb * 1024 * 1024) / sizeof(TTEntry);
        // Round down to power of 2
        size_t power = 1;
        while(power * 2 <= num_entries) power *= 2;
        num_entries = power;
        mask = num_entries - 1;
        table.resize(num_entries);
    }
    
    bool probe(uint64_t hash, int depth, uint64_t& count) {
        size_t idx = hash & mask;
        TTEntry& entry = table[idx];
        if(entry.valid && entry.hash == hash && entry.depth == depth) {
            hits.fetch_add(1, std::memory_order_relaxed);
            count = entry.count;
            return true;
        }
        misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    void store(uint64_t hash, int depth, uint64_t count) {
        size_t idx = hash & mask;
        TTEntry& entry = table[idx];
        // Always replace (could use more sophisticated replacement policy)
        entry.hash = hash;
        entry.depth = depth;
        entry.count = count;
        entry.valid = true;
    }
    
    std::pair<uint64_t, uint64_t> get_stats() const {
        return {hits.load(), misses.load()};
    }
};

// Perft worker with transposition table
static uint64_t perft_worker_tt(const state& s, int depth, TranspositionTable& tt)
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Compute hash for this state
    uint64_t hash = compute_state_hash(s);
    
    // Try to find in transposition table
    uint64_t cached_count;
    if(tt.probe(hash, depth, cached_count))
    {
        return cached_count;
    }
    
    // Check if current player can submit
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        if(depth == 1)
        {
            tt.store(hash, depth, 1);
            return 1;
        }
        uint64_t count = perft_worker_tt(*submit_state, depth - 1, tt);
        tt.store(hash, depth, count);
        return count;
    }
    
    auto [w, ss] = HC_info::build_HC(s);
    uint64_t count = 0;
    
    for(moveseq mvs : w.search(ss))
    {
        if(depth == 1)
        {
            ++count;
        }
        else
        {
            std::vector<ext_move> emvs;
            std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
                return ext_move(m);
            });
            action act = action::from_vector(emvs, s);
            auto new_state_opt = s.can_apply(act);
            if(new_state_opt.has_value())
            {
                auto submitted = new_state_opt->can_submit();
                if(submitted.has_value())
                {
                    count += perft_worker_tt(*submitted, depth - 1, tt);
                }
                else
                {
                    count += perft_worker_tt(*new_state_opt, depth - 1, tt);
                }
            }
        }
    }
    
    tt.store(hash, depth, count);
    return count;
}

uint64_t game::perft_with_tt(int depth, unsigned int num_threads, size_t tt_size_mb) const
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Auto-detect number of threads if not specified
    if(num_threads == 0)
    {
        num_threads = std::thread::hardware_concurrency();
        if(num_threads == 0) num_threads = 4;
    }
    
    const state& s = current_node->get_state();
    
    // For depth 1, just count directly
    if(depth == 1)
    {
        return count_actions();
    }
    
    // Check if current player can submit
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        TranspositionTable tt(tt_size_mb);
        return perft_worker_tt(*submit_state, depth - 1, tt);
    }
    
    // Collect all first-level actions
    std::vector<PerftTask> tasks;
    auto [w, ss] = HC_info::build_HC(s);
    
    for(moveseq mvs : w.search(ss))
    {
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
            return ext_move(m);
        });
        action act = action::from_vector(emvs, s);
        auto new_state_opt = s.can_apply(act);
        if(new_state_opt.has_value())
        {
            auto submitted = new_state_opt->can_submit();
            if(submitted.has_value())
            {
                tasks.push_back({*submitted, depth - 1});
            }
            else
            {
                tasks.push_back({*new_state_opt, depth - 1});
            }
        }
    }
    
    if(tasks.empty())
    {
        return 0;
    }
    
    // Each thread gets its own transposition table to avoid lock contention
    // The trade-off is less sharing, but no synchronization overhead
    std::atomic<uint64_t> total_count{0};
    std::atomic<size_t> task_index{0};
    
    size_t per_thread_tt_size = tt_size_mb / num_threads;
    if(per_thread_tt_size < 16) per_thread_tt_size = 16; // Minimum 16 MB per thread
    
    auto worker = [&]() {
        TranspositionTable local_tt(per_thread_tt_size);
        uint64_t local_count = 0;
        
        while(true)
        {
            size_t idx = task_index.fetch_add(1, std::memory_order_relaxed);
            if(idx >= tasks.size())
            {
                break;
            }
            local_count += perft_worker_tt(tasks[idx].s, tasks[idx].depth, local_tt);
        }
        total_count.fetch_add(local_count, std::memory_order_relaxed);
    };
    
    unsigned int actual_threads = std::min(num_threads, static_cast<unsigned int>(tasks.size()));
    
    std::vector<std::thread> threads;
    threads.reserve(actual_threads);
    
    for(unsigned int i = 0; i < actual_threads; ++i)
    {
        threads.emplace_back(worker);
    }
    
    for(auto& t : threads)
    {
        t.join();
    }
    
    return total_count.load();
}

//////////////////////////////////////////////
// Dynamic Work-Stealing Perft
//////////////////////////////////////////////

// Thread-safe task queue for dynamic work distribution
class TaskQueue {
    std::queue<std::shared_ptr<PerftTask>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    std::atomic<int> active_workers{0};
    
public:
    void push(state s, int depth) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.push(std::make_shared<PerftTask>(PerftTask{std::move(s), depth}));
        }
        cv.notify_one();
    }
    
    void push_batch(std::vector<PerftTask>& batch) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            for(auto& task : batch) {
                tasks.push(std::make_shared<PerftTask>(std::move(task)));
            }
        }
        cv.notify_all();
    }
    
    std::shared_ptr<PerftTask> try_pop() {
        std::lock_guard<std::mutex> lock(mtx);
        if(tasks.empty()) {
            return nullptr;
        }
        auto task = tasks.front();
        tasks.pop();
        return task;
    }
    
    void finish() {
        done.store(true);
        cv.notify_all();
    }
    
    bool is_done() const {
        return done.load();
    }
    
    void worker_start() {
        active_workers.fetch_add(1, std::memory_order_relaxed);
    }
    
    void worker_end() {
        active_workers.fetch_sub(1, std::memory_order_relaxed);
    }
    
    bool all_idle_and_empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return tasks.empty() && active_workers.load() == 0;
    }
};

// Dynamic perft worker that can spawn subtasks
static uint64_t perft_worker_dynamic(const state& s, int depth, int split_depth, TaskQueue& queue)
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Check if current player can submit
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        if(depth == 1)
        {
            return 1;
        }
        return perft_worker_dynamic(*submit_state, depth - 1, split_depth, queue);
    }
    
    auto [w, ss] = HC_info::build_HC(s);
    uint64_t count = 0;
    
    // If we're at split depth and have remaining depth, create parallel tasks
    bool should_split = (depth >= split_depth && depth > 1);
    std::vector<PerftTask> subtasks;
    
    for(moveseq mvs : w.search(ss))
    {
        if(depth == 1)
        {
            ++count;
        }
        else
        {
            std::vector<ext_move> emvs;
            std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
                return ext_move(m);
            });
            action act = action::from_vector(emvs, s);
            auto new_state_opt = s.can_apply(act);
            if(new_state_opt.has_value())
            {
                state next_state = *new_state_opt;
                auto submitted = next_state.can_submit();
                if(submitted.has_value())
                {
                    next_state = *submitted;
                }
                
                if(should_split)
                {
                    // Push to task queue for other threads to pick up
                    subtasks.push_back({next_state, depth - 1});
                }
                else
                {
                    // Process locally
                    count += perft_worker(next_state, depth - 1);
                }
            }
        }
    }
    
    if(!subtasks.empty())
    {
        // Push all subtasks at once
        queue.push_batch(subtasks);
    }
    
    return count;
}

uint64_t game::perft_dynamic(int depth, unsigned int num_threads, int split_depth) const
{
    if(depth <= 0)
    {
        return 1;
    }
    
    // Auto-detect number of threads if not specified
    if(num_threads == 0)
    {
        num_threads = std::thread::hardware_concurrency();
        if(num_threads == 0) num_threads = 4;
    }
    
    const state& s = current_node->get_state();
    
    // For depth 1, just count directly
    if(depth == 1)
    {
        return count_actions();
    }
    
    // Task queue for dynamic work distribution
    TaskQueue queue;
    std::atomic<uint64_t> total_count{0};
    std::atomic<bool> all_done{false};
    
    // Initial task
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        queue.push(*submit_state, depth - 1);
    }
    else
    {
        queue.push(s, depth);
    }
    
    auto worker = [&]() {
        uint64_t local_count = 0;
        
        while(!all_done.load(std::memory_order_relaxed))
        {
            auto task_ptr = queue.try_pop();
            if(task_ptr)
            {
                queue.worker_start();
                local_count += perft_worker_dynamic(task_ptr->s, task_ptr->depth, split_depth, queue);
                queue.worker_end();
            }
            else
            {
                // No task available, check if we should exit
                if(queue.all_idle_and_empty())
                {
                    all_done.store(true, std::memory_order_relaxed);
                    break;
                }
                // Yield to other threads
                std::this_thread::yield();
            }
        }
        
        total_count.fetch_add(local_count, std::memory_order_relaxed);
    };
    
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    
    for(unsigned int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker);
    }
    
    for(auto& t : threads)
    {
        t.join();
    }
    
    return total_count.load();
}

// Helper for perft_timed: worker with stop flag
static uint64_t perft_worker_timed(const state& s, int depth, const std::atomic<bool>& stop_flag)
{
    if(stop_flag.load(std::memory_order_relaxed))
    {
        return 0; // Early exit if stopped
    }
    
    if(depth <= 0)
    {
        return 1;
    }
    
    // Check if current player can submit
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        if(depth == 1)
        {
            return 1;
        }
        return perft_worker_timed(*submit_state, depth - 1, stop_flag);
    }
    
    auto [w, ss] = HC_info::build_HC(s);
    uint64_t count = 0;
    
    for(moveseq mvs : w.search(ss))
    {
        if(stop_flag.load(std::memory_order_relaxed))
        {
            break; // Check periodically and exit early if stopped
        }
        
        if(depth == 1)
        {
            ++count;
        }
        else
        {
            std::vector<ext_move> emvs;
            std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
                return ext_move(m);
            });
            action act = action::from_vector(emvs, s);
            auto new_state_opt = s.can_apply(act);
            if(new_state_opt.has_value())
            {
                auto submitted = new_state_opt->can_submit();
                if(submitted.has_value())
                {
                    count += perft_worker_timed(*submitted, depth - 1, stop_flag);
                }
                else
                {
                    count += perft_worker_timed(*new_state_opt, depth - 1, stop_flag);
                }
            }
        }
    }
    
    return count;
}

std::pair<uint64_t, bool> game::perft_timed(int depth, double timeout_seconds, unsigned int num_threads) const
{
    if(depth <= 0)
    {
        return {1, true};
    }
    
    // Auto-detect number of threads if not specified
    if(num_threads == 0)
    {
        num_threads = std::thread::hardware_concurrency();
        if(num_threads == 0) num_threads = 4;
    }
    
    const state& s = current_node->get_state();
    
    // For depth 1, just count directly (always completes quickly)
    if(depth == 1)
    {
        return {count_actions(), true};
    }
    
    // Get the starting state (after submit if possible)
    state start_state = s;
    int start_depth = depth;
    auto submit_state = s.can_submit();
    if(submit_state.has_value())
    {
        start_state = *submit_state;
        start_depth = depth - 1;
    }
    
    // Generate first-level tasks for parallel distribution
    std::vector<PerftTask> tasks;
    auto [w, ss] = HC_info::build_HC(start_state);
    
    for(moveseq mvs : w.search(ss))
    {
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [](full_move m){
            return ext_move(m);
        });
        action act = action::from_vector(emvs, start_state);
        auto new_state_opt = start_state.can_apply(act);
        if(new_state_opt.has_value())
        {
            auto submitted = new_state_opt->can_submit();
            if(submitted.has_value())
            {
                tasks.push_back({*submitted, start_depth - 1});
            }
            else
            {
                tasks.push_back({*new_state_opt, start_depth - 1});
            }
        }
    }
    
    if(tasks.empty())
    {
        return {0, true};
    }
    
    // Atomic counters and stop flag
    std::atomic<uint64_t> total_count{0};
    std::atomic<size_t> next_task_idx{0};
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> completed{true};
    
    // Start time
    auto start_time = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::duration<double>(timeout_seconds);
    
    // Timer thread to check timeout
    std::thread timer_thread([&]() {
        while(!stop_flag.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if(elapsed >= timeout_duration)
            {
                completed.store(false, std::memory_order_relaxed);
                stop_flag.store(true, std::memory_order_relaxed);
                break;
            }
        }
    });
    
    auto worker = [&]() {
        while(!stop_flag.load(std::memory_order_relaxed))
        {
            size_t idx = next_task_idx.fetch_add(1, std::memory_order_relaxed);
            if(idx >= tasks.size())
            {
                break;
            }
            
            const auto& task = tasks[idx];
            uint64_t count = perft_worker_timed(task.s, task.depth, stop_flag);
            total_count.fetch_add(count, std::memory_order_relaxed);
        }
    };
    
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    
    for(unsigned int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker);
    }
    
    for(auto& t : threads)
    {
        t.join();
    }
    
    // Signal timer thread to stop and wait for it
    stop_flag.store(true, std::memory_order_relaxed);
    timer_thread.join();
    
    return {total_count.load(), completed.load()};
}

/////////////////////////////
// Comments and navigation //
/////////////////////////////

game::comments_t game::get_comments() const
{
    return current_node->get_info();
}

bool game::has_parent() const
{
    return current_node->get_parent() != nullptr;
}

void game::visit_parent()
{
    if(!has_parent())
        return;
    current_node = current_node->get_parent();
    fresh();
}

std::vector<std::tuple<action, std::string>> game::get_child_moves() const
{
    std::vector<std::tuple<action, std::string>> result;
    auto &children = current_node->get_children();
    state s = current_node->get_state();
    for(const auto &child : children)
    {
        const action &act = child->get_action();
        std::string txt = s.pretty_action(act);
        result.push_back({act, txt});
    }
    return result;
}

bool game::visit_child(action act, comments_t comments, std::optional<state> newstate)
{
    // check if the child already exists
    auto &children = current_node->get_children();
    for(auto &child : children)
    {
        if(child->get_action().get_moves() == act.get_moves())
        {
            current_node = child.get();
            fresh();
            return true;
        }
    }
    // create a new child
    auto new_child = gnode<comments_t>::create_child(current_node, newstate, act, comments);
    current_node = current_node->add_child(std::move(new_child));
    fresh();
    return false;
}

std::string game::show_pgn()
{
    std::ostringstream oss;
    for(const auto &[k, v] : metadata)
    {
        std::string key = k;
        if(!key.empty())
        {
            key[0] = toupper(key[0]);
        }
        oss << "[" << key << " \"" << v << "\"]\n";
    }
    oss << gametree->get_state().show_fen() << "\n";
    oss << gametree->to_string(show_comments);
    return oss.str();
}
