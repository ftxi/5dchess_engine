// bot.h
// AI Bot for 5D Chess with Multiverse Time Travel
// Created for the 5dchess_engine project

#ifndef BOT_H
#define BOT_H

#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <atomic>
#include <memory>
#include <functional>
#include <random>
#include <optional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "state.h"
#include "hypercuboid.h"
#include "action.h"

namespace bot {

// Forward declarations
struct BotConfig;
struct SearchStats;

/*
 * Evaluation score type
 * Using centipawns (1 pawn = 100)
 */
using score_t = int32_t;
constexpr score_t SCORE_INFINITY = 1000000;
constexpr score_t SCORE_MATE = 900000;
constexpr score_t SCORE_DRAW = 0;

/*
 * BotConfig: Configuration parameters for the bot
 */
struct BotConfig
{
    // Search limits
    int max_depth = 4;                    // Maximum search depth
    uint64_t max_nodes = 1000000;         // Maximum nodes to search
    double time_limit_ms = 10000.0;       // Time limit in milliseconds
    
    // Search options
    bool use_iterative_deepening = true;  // Use iterative deepening
    bool use_transposition_table = true;  // Use transposition table
    bool use_move_ordering = true;        // Use move ordering heuristics
    bool use_null_move_pruning = false;   // Null move pruning (disabled for 5D chess)
    bool use_late_move_reduction = true;  // Late move reduction
    bool use_quiescence = false;          // Quiescence search (expensive in 5D)
    
    // Action sampling for large branching factors
    bool use_action_sampling = true;      // Sample actions instead of full enumeration
    int max_actions_per_ply = 500;        // Maximum actions to consider per ply
    int beam_width = 100;                 // Beam width for action sampling
    
    // Transposition table settings
    size_t tt_size_mb = 128;              // Transposition table size in MB
    
    // Multi-threading settings
    unsigned int num_threads = 0;         // 0 = auto-detect (use hardware_concurrency)
    bool use_parallel_search = true;      // Use Lazy SMP parallel search
    
    // Debug options
    bool verbose = false;                 // Print search info
    
    BotConfig() = default;
};

/*
 * SearchStats: Statistics from the search
 */
struct SearchStats
{
    std::atomic<uint64_t> nodes_searched{0};
    std::atomic<uint64_t> tt_hits{0};
    std::atomic<uint64_t> tt_cutoffs{0};
    int depth_reached = 0;
    double time_elapsed_ms = 0.0;
    score_t best_score = 0;
    bool search_complete = false;
    
    void reset() {
        nodes_searched.store(0, std::memory_order_relaxed);
        tt_hits.store(0, std::memory_order_relaxed);
        tt_cutoffs.store(0, std::memory_order_relaxed);
        depth_reached = 0;
        time_elapsed_ms = 0.0;
        best_score = 0;
        search_complete = false;
    }
    
    // For Python binding - get non-atomic values
    uint64_t get_nodes_searched() const { return nodes_searched.load(std::memory_order_relaxed); }
    uint64_t get_tt_hits() const { return tt_hits.load(std::memory_order_relaxed); }
    uint64_t get_tt_cutoffs() const { return tt_cutoffs.load(std::memory_order_relaxed); }
};

/*
 * TranspositionTable entry
 * Using atomic for lock-free multi-threaded access
 */
struct TTEntry
{
    std::atomic<uint64_t> hash{0};
    std::atomic<int32_t> score{0};
    std::atomic<int16_t> depth{0};
    std::atomic<uint8_t> flag{0};  // 0=exact, 1=lower, 2=upper
    std::atomic<uint8_t> age{0};   // For replacement policy
    moveseq best_move;             // Not atomic, protected by hash verification
    
    static constexpr uint8_t FLAG_EXACT = 0;
    static constexpr uint8_t FLAG_LOWER = 1;
    static constexpr uint8_t FLAG_UPPER = 2;
    
    TTEntry() = default;
    
    // Copy constructor for moveseq
    TTEntry(const TTEntry& other) 
        : hash(other.hash.load(std::memory_order_relaxed))
        , score(other.score.load(std::memory_order_relaxed))
        , depth(other.depth.load(std::memory_order_relaxed))
        , flag(other.flag.load(std::memory_order_relaxed))
        , age(other.age.load(std::memory_order_relaxed))
        , best_move(other.best_move)
    {}
    
    TTEntry& operator=(const TTEntry& other) {
        hash.store(other.hash.load(std::memory_order_relaxed), std::memory_order_relaxed);
        score.store(other.score.load(std::memory_order_relaxed), std::memory_order_relaxed);
        depth.store(other.depth.load(std::memory_order_relaxed), std::memory_order_relaxed);
        flag.store(other.flag.load(std::memory_order_relaxed), std::memory_order_relaxed);
        age.store(other.age.load(std::memory_order_relaxed), std::memory_order_relaxed);
        best_move = other.best_move;
        return *this;
    }
};

/*
 * Transposition Table with lock-free multi-threaded support
 */
class TranspositionTable
{
    std::vector<TTEntry> table;
    size_t mask;
    std::atomic<uint8_t> current_age{0};
    
public:
    TranspositionTable(size_t size_mb = 128);
    void clear();
    void new_search(); // Increment age for new search
    void store(uint64_t hash, score_t score, int depth, uint8_t flag, const moveseq& best_move);
    bool probe(uint64_t hash, TTEntry& out_entry) const;
    size_t size() const { return table.size(); }
    std::pair<uint64_t, uint64_t> get_stats() const; // hits, total probes
};

/*
 * Evaluator: Evaluates a game state
 */
class Evaluator
{
public:
    /*
     * evaluate(): Main evaluation function
     * Returns score from the perspective of the side to move
     * Positive = good for side to move, negative = bad
     */
    static score_t evaluate(const state& s);
    
    /*
     * Evaluation components
     */
    static score_t evaluate_material(const state& s, bool color);
    static score_t evaluate_timeline_control(const state& s, bool color);
    static score_t evaluate_mobility(const state& s, bool color);
    static score_t evaluate_king_safety(const state& s, bool color);
    static score_t evaluate_threats(const state& s, bool color);
    static score_t evaluate_tempo(const state& s, bool color);
    
    /*
     * Piece values (adjusted for 5D chess)
     */
    static constexpr score_t PAWN_VALUE = 100;
    static constexpr score_t KNIGHT_VALUE = 320;
    static constexpr score_t BISHOP_VALUE = 330;
    static constexpr score_t ROOK_VALUE = 500;
    static constexpr score_t QUEEN_VALUE = 900;
    static constexpr score_t UNICORN_VALUE = 400;    // Diagonal in T-L plane
    static constexpr score_t DRAGON_VALUE = 600;     // Super-sliding
    static constexpr score_t PRINCESS_VALUE = 700;   // Bishop + Rook
    static constexpr score_t BRAWN_VALUE = 150;      // Enhanced pawn
    static constexpr score_t COMMON_KING_VALUE = 350;
    static constexpr score_t ROYAL_QUEEN_VALUE = 1000;
    
    static score_t get_piece_value(piece_t piece);
    
private:
    static score_t get_mobility_score(const state& s, vec4 pos, bool color);
};

/*
 * ActionSampler: Samples promising actions from the action space
 */
class ActionSampler
{
public:
    /*
     * sample_actions(): Sample and rank promising actions
     * Returns a vector of (moveseq, priority) pairs
     */
    static std::vector<std::pair<moveseq, score_t>> sample_actions(
        const state& s,
        const HC_info& hc_info,
        search_space& ss,
        int max_actions,
        std::mt19937& rng);
    
    /*
     * score_action(): Heuristic score for an action (for move ordering)
     */
    static score_t score_action(const state& s, const moveseq& action);
    
private:
    // Action scoring components
    static score_t score_capture(const state& s, const moveseq& action);
    static score_t score_check(const state& s, const moveseq& action);
    static score_t score_timeline_jump(const state& s, const moveseq& action);
    static score_t score_center_control(const state& s, const moveseq& action);
};

/*
 * Bot: Main AI class with multi-threaded search support (Lazy SMP)
 */
class Bot
{
public:
    Bot(BotConfig config = BotConfig());
    ~Bot();
    
    /*
     * find_best_action(): Find the best action for the current state
     * Returns the best action found within the search limits
     */
    std::optional<action> find_best_action(const state& s);
    
    /*
     * search_with_callback(): Search with progress callback
     */
    std::optional<action> search_with_callback(
        const state& s,
        std::function<void(int depth, score_t score, const moveseq& pv)> callback);
    
    /*
     * Accessors
     */
    const SearchStats& get_stats() const { return stats; }
    BotConfig& get_config() { return config; }
    const BotConfig& get_config() const { return config; }
    
    /*
     * stop(): Signal the bot to stop searching
     */
    void stop() { should_stop.store(true, std::memory_order_release); }
    
private:
    BotConfig config;
    SearchStats stats;
    TranspositionTable tt;
    std::atomic<bool> should_stop{false};
    std::chrono::steady_clock::time_point search_start;
    
    // Best move tracking for parallel search
    std::mutex best_move_mutex;
    moveseq global_best_move;
    std::atomic<score_t> global_best_score{-SCORE_INFINITY};
    std::atomic<int> completed_depth{0};
    
    // Search methods
    score_t negamax(
        state& s,
        int depth,
        score_t alpha,
        score_t beta,
        moveseq& pv,
        int thread_id,
        std::mt19937& rng);
    
    // Parallel search worker
    void search_worker(
        const state& s,
        int target_depth,
        int thread_id);
    
    // Root search for a specific depth (used by workers)
    void search_root(
        const state& s,
        int depth,
        int thread_id,
        std::mt19937& rng);
    
    // Utility methods
    bool time_up() const;
    bool should_stop_search() const;
    uint64_t compute_hash(const state& s) const;
    
    // Principal variation
    std::vector<moveseq> principal_variation;
};

/*
 * Utility: Hash computation for states
 */
class ZobristHash
{
public:
    static ZobristHash& instance();
    uint64_t hash(const state& s) const;
    uint64_t hash_incremental(uint64_t prev_hash, const full_move& m, const state& s) const;
    
private:
    ZobristHash();
    std::vector<std::vector<uint64_t>> piece_square; // [piece][position]
    uint64_t side_to_move;
    std::vector<uint64_t> timeline_keys;
};

} // namespace bot

#endif // BOT_H
