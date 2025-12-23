// game.h
// interface for python library
#ifndef GAME_H
#define GAME_H

#include <vector>
#include <tuple>
#include <string>
#include <map>
#include <set>
#include <functional>
#include "state.h"
#include "gametree.h"

class game
{
    using comments_t = std::vector<std::string>;
    std::unique_ptr<gnode<comments_t>> gametree;
    gnode<comments_t> *current_node;
    using cache_t = std::pair<state,std::optional<ext_move>>;
    std::vector<cache_t> cached;
    std::vector<cache_t>::iterator now;
    
    game(std::unique_ptr<gnode<comments_t>> gt);
    void fresh();
public:
    std::map<std::string, std::string> metadata;
    
    static game from_pgn(std::string str);
    
    state get_current_state() const;
    std::pair<int, bool> get_current_present() const;
    std::vector<std::tuple<int,int,bool,std::string>> get_current_boards() const;
    std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> get_current_timeline_status() const;
    std::vector<vec4> gen_move_if_playable(vec4 p);
    
    match_status_t get_match_status() const;
    std::vector<vec4> get_movable_pieces() const;
    
    bool is_playable(vec4 p) const;
    bool can_undo() const;
    bool can_redo() const;
    bool can_submit() const;
    bool undo();
    bool redo();
    bool apply_move(ext_move m);
    bool submit();
    bool currently_check() const;
    std::vector<std::pair<vec4,vec4>> get_current_checks() const;
    std::pair<int, int> get_board_size() const;
    
    bool suggest_action();
    
    /*
    perft: Count the number of legal actions at the current state up to given depth.
    - depth=1: count all legal actions from current state
    - depth=2: count all (action, response) pairs
    - callback: optional progress callback function(current_count, total_depth1_count)
    Returns total count of leaf nodes at the specified depth.
    */
    uint64_t perft(int depth, std::function<void(uint64_t, uint64_t)> callback = nullptr) const;
    
    /*
    perft_parallel: Multi-threaded version of perft for better CPU utilization.
    - depth: search depth
    - num_threads: number of threads to use (0 = auto-detect based on hardware)
    Returns total count of leaf nodes at the specified depth.
    */
    uint64_t perft_parallel(int depth, unsigned int num_threads = 0) const;
    
    /*
    perft_with_tt: Perft with transposition table for faster repeated state lookup.
    Uses hash-based caching to avoid recalculating identical positions.
    - depth: search depth
    - num_threads: number of threads (0 = auto-detect)
    - tt_size_mb: transposition table size in megabytes (default 256 MB)
    Returns total count of leaf nodes at the specified depth.
    */
    uint64_t perft_with_tt(int depth, unsigned int num_threads = 0, size_t tt_size_mb = 256) const;
    
    /*
    perft_dynamic: Dynamic work-stealing parallel perft.
    Uses a work queue that dynamically distributes tasks across threads.
    More efficient for unbalanced search trees.
    - depth: search depth
    - num_threads: number of threads (0 = auto-detect)
    - split_depth: depth at which to create new parallel tasks (default 2)
    Returns total count of leaf nodes at the specified depth.
    */
    uint64_t perft_dynamic(int depth, unsigned int num_threads = 0, int split_depth = 2) const;
    
    /*
    perft_timed: Perft with timeout mechanism.
    Runs perft search until either completion or timeout is reached.
    - depth: search depth
    - timeout_seconds: maximum time to run (in seconds)
    - num_threads: number of threads (0 = auto-detect)
    Returns a pair of (count, completed):
    - count: number of leaf nodes found (partial if timed out)
    - completed: true if search finished, false if timed out
    */
    std::pair<uint64_t, bool> perft_timed(int depth, double timeout_seconds, unsigned int num_threads = 0) const;
    
    /*
    count_actions: Count the number of legal actions at the current state (depth=1).
    This is a faster version of perft(1).
    */
    uint64_t count_actions() const;

    comments_t get_comments() const;
    //TODO: implement comment editing functions
    bool has_parent() const;
    void visit_parent();
    std::vector<std::tuple<action, std::string>> get_child_moves() const;
    /*
    visit_child:
    visit a child node (will create one if that child doesn't exist)
    returns true if the child exists; false if a new child is created
    */
    bool visit_child(action act, comments_t comments = {}, std::optional<state> newstate = std::nullopt);
    
    std::string show_pgn();
};



#endif // GAME_H
