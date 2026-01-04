// bot.cpp
// AI Bot implementation for 5D Chess with Multiverse Time Travel

#include "bot.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <thread>

namespace bot {

//=============================================================================
// TranspositionTable Implementation (Lock-Free Multi-Threaded)
//=============================================================================

TranspositionTable::TranspositionTable(size_t size_mb)
{
    size_t num_entries = (size_mb * 1024 * 1024) / sizeof(TTEntry);
    // Round down to power of 2
    size_t power = 1;
    while (power * 2 <= num_entries) power *= 2;
    table.resize(power);
    mask = power - 1;
}

void TranspositionTable::clear()
{
    for (auto& entry : table) {
        entry.hash.store(0, std::memory_order_relaxed);
        entry.score.store(0, std::memory_order_relaxed);
        entry.depth.store(0, std::memory_order_relaxed);
        entry.flag.store(0, std::memory_order_relaxed);
        entry.age.store(0, std::memory_order_relaxed);
        entry.best_move.clear();
    }
}

void TranspositionTable::new_search()
{
    current_age.fetch_add(1, std::memory_order_relaxed);
}

void TranspositionTable::store(uint64_t hash, score_t score, int depth, uint8_t flag, const moveseq& best_move)
{
    size_t idx = hash & mask;
    TTEntry& entry = table[idx];
    
    uint64_t old_hash = entry.hash.load(std::memory_order_relaxed);
    int16_t old_depth = entry.depth.load(std::memory_order_relaxed);
    uint8_t old_age = entry.age.load(std::memory_order_relaxed);
    uint8_t cur_age = current_age.load(std::memory_order_relaxed);
    
    // Replace if: 
    // 1. Empty entry
    // 2. Same position (update with deeper search or exact bound)
    // 3. Old entry from previous search
    // 4. Deeper search depth
    bool should_replace = (old_hash == 0) ||
                          (old_hash == hash) ||
                          (old_age != cur_age) ||
                          (depth >= old_depth);
    
    if (should_replace) {
        // Store in specific order to minimize races
        entry.best_move = best_move;
        entry.flag.store(flag, std::memory_order_relaxed);
        entry.depth.store(static_cast<int16_t>(depth), std::memory_order_relaxed);
        entry.score.store(static_cast<int32_t>(score), std::memory_order_relaxed);
        entry.age.store(cur_age, std::memory_order_relaxed);
        entry.hash.store(hash, std::memory_order_release); // Release for visibility
    }
}

bool TranspositionTable::probe(uint64_t hash, TTEntry& out_entry) const
{
    size_t idx = hash & mask;
    const TTEntry& entry = table[idx];
    
    // Read hash first with acquire semantics
    uint64_t stored_hash = entry.hash.load(std::memory_order_acquire);
    
    if (stored_hash == hash) {
        // Copy data - note: there's a small race window here for best_move
        // but it's acceptable for Lazy SMP (we'll verify with hash)
        out_entry.hash.store(stored_hash, std::memory_order_relaxed);
        out_entry.score.store(entry.score.load(std::memory_order_relaxed), std::memory_order_relaxed);
        out_entry.depth.store(entry.depth.load(std::memory_order_relaxed), std::memory_order_relaxed);
        out_entry.flag.store(entry.flag.load(std::memory_order_relaxed), std::memory_order_relaxed);
        out_entry.age.store(entry.age.load(std::memory_order_relaxed), std::memory_order_relaxed);
        out_entry.best_move = entry.best_move;
        
        // Verify hash hasn't changed during read
        if (entry.hash.load(std::memory_order_relaxed) == stored_hash) {
            return true;
        }
    }
    return false;
}

std::pair<uint64_t, uint64_t> TranspositionTable::get_stats() const
{
    uint64_t filled = 0;
    for (const auto& entry : table) {
        if (entry.hash.load(std::memory_order_relaxed) != 0) {
            filled++;
        }
    }
    return {filled, table.size()};
}

//=============================================================================
// ZobristHash Implementation
//=============================================================================

ZobristHash& ZobristHash::instance()
{
    static ZobristHash instance;
    return instance;
}

ZobristHash::ZobristHash()
{
    std::mt19937_64 rng(0xDEADBEEF5DC12345ULL);
    std::uniform_int_distribution<uint64_t> dist;
    
    // Initialize piece-square table
    // 32 piece types x 64 squares x 256 possible (L,T) combinations
    piece_square.resize(256);
    for (auto& ps : piece_square) {
        ps.resize(64 * 256);
        for (auto& v : ps) {
            v = dist(rng);
        }
    }
    
    side_to_move = dist(rng);
    
    // Timeline keys
    timeline_keys.resize(256);
    for (auto& k : timeline_keys) {
        k = dist(rng);
    }
}

uint64_t ZobristHash::hash(const state& s) const
{
    uint64_t h = 0;
    
    auto [present_t, present_c] = s.get_present();
    
    // Hash side to move
    if (present_c) {
        h ^= side_to_move;
    }
    
    // Hash all boards
    auto boards = s.get_boards();
    for (const auto& [l, t, c, fen] : boards) {
        auto board_ptr = s.get_board(l, t, c);
        if (!board_ptr) continue;
        
        // Simple hash based on board coordinates and content
        uint64_t board_hash = timeline_keys[(l + 128) & 0xFF];
        board_hash ^= timeline_keys[(t + 128) & 0xFF] << 8;
        board_hash ^= c ? side_to_move : 0;
        
        // XOR with a hash of the FEN string
        std::hash<std::string> string_hasher;
        board_hash ^= string_hasher(fen);
        
        h ^= board_hash;
    }
    
    return h;
}

uint64_t ZobristHash::hash_incremental(uint64_t prev_hash, const full_move& m, const state& s) const
{
    // For simplicity, just compute full hash
    // TODO: Implement proper incremental hashing
    return hash(s);
}

//=============================================================================
// Evaluator Implementation
//=============================================================================

score_t Evaluator::get_piece_value(piece_t piece)
{
    piece_t p = piece_name(piece);
    switch (to_white(p)) {
        case PAWN_W:    return PAWN_VALUE;
        case KNIGHT_W:  return KNIGHT_VALUE;
        case BISHOP_W:  return BISHOP_VALUE;
        case ROOK_W:    return ROOK_VALUE;
        case QUEEN_W:   return QUEEN_VALUE;
        case KING_W:    return 0; // King has infinite value, not counted in material
        case UNICORN_W: return UNICORN_VALUE;
        case DRAGON_W:  return DRAGON_VALUE;
        case PRINCESS_W: return PRINCESS_VALUE;
        case BRAWN_W:   return BRAWN_VALUE;
        case COMMON_KING_W: return COMMON_KING_VALUE;
        case ROYAL_QUEEN_W: return ROYAL_QUEEN_VALUE;
        default:        return 0;
    }
}

score_t Evaluator::evaluate_material(const state& s, bool color)
{
    score_t score = 0;
    auto boards = s.get_boards();
    auto [present_t, present_c] = s.get_present();
    auto [l_min, l_max] = s.get_lines_range();
    
    for (const auto& [l, t, c, fen] : boards) {
        // Only count pieces on present boards (most recent state)
        auto [end_t, end_c] = s.get_timeline_end(l);
        if (t != end_t || c != end_c) continue;
        
        auto board_ptr = s.get_board(l, t, c);
        if (!board_ptr) continue;
        
        // Count pieces on this board
        for (int pos = 0; pos < 64; ++pos) {
            piece_t piece = board_ptr->get_piece(pos);
            if (piece == NO_PIECE || piece == WALL_PIECE) continue;
            
            score_t piece_value = get_piece_value(piece);
            
            // Apply timeline weight: pieces on active timelines are more valuable
            double timeline_weight = 1.0;
            auto [active_min, active_max] = s.get_active_range();
            if (l >= active_min && l <= active_max) {
                timeline_weight = 1.2; // 20% bonus for active timeline pieces
            }
            
            if (piece_color(piece) == color) {
                score += static_cast<score_t>(piece_value * timeline_weight);
            } else {
                score -= static_cast<score_t>(piece_value * timeline_weight);
            }
        }
    }
    
    return score;
}

score_t Evaluator::evaluate_timeline_control(const state& s, bool color)
{
    score_t score = 0;
    auto [l_min, l_max] = s.get_lines_range();
    auto [active_min, active_max] = s.get_active_range();
    auto [present_t, present_c] = s.get_present();
    
    // Number of timelines controlled
    int player_timelines = 0;
    int opponent_timelines = 0;
    
    for (int l = l_min; l <= l_max; ++l) {
        auto [end_t, end_c] = s.get_timeline_end(l);
        if (end_c == color) {
            player_timelines++;
        } else {
            opponent_timelines++;
        }
    }
    
    // Having more timelines to move on is generally advantageous
    // But too many can be overwhelming
    int timeline_diff = player_timelines - opponent_timelines;
    
    // Penalty for having many mandatory moves
    auto [mandatory, optional, unplayable] = s.get_timeline_status();
    if (present_c == color) {
        score -= static_cast<score_t>(mandatory.size()) * 10; // Penalty for each mandatory timeline
    } else {
        score += static_cast<score_t>(mandatory.size()) * 10;
    }
    
    // Bonus for timeline spread (attacking multiple timelines)
    score += timeline_diff * 20;
    
    // Active range control
    int active_span = active_max - active_min + 1;
    if (active_span > 1) {
        score += 15 * active_span; // Bonus for wider active range
    }
    
    return score;
}

score_t Evaluator::evaluate_mobility(const state& s, bool color)
{
    score_t score = 0;
    auto [present_t, present_c] = s.get_present();
    
    // Count movable pieces
    if (present_c == color) {
        auto movable = s.gen_movable_pieces();
        score += static_cast<score_t>(movable.size()) * 5;
        
        // Sample some pieces and count their moves
        int sample_count = std::min(static_cast<int>(movable.size()), 10);
        for (int i = 0; i < sample_count; ++i) {
            vec4 pos = movable[i];
            int move_count = 0;
            for ([[maybe_unused]] const auto& _ : s.gen_piece_move(pos)) {
                move_count++;
                if (move_count > 20) break; // Cap for performance
            }
            score += move_count * 2;
        }
    }
    
    return score;
}

score_t Evaluator::evaluate_king_safety(const state& s, bool color)
{
    score_t score = 0;
    auto [present_t, present_c] = s.get_present();
    
    // Check if we're in check
    if (present_c != color) {
        auto checks = s.find_checks(color);
        if (checks.first().has_value()) {
            score -= 50; // Penalty for being in check
        }
    }
    
    // TODO: Evaluate king position, pawn shield, etc.
    // This is complex in 5D chess due to the temporal dimension
    
    return score;
}

score_t Evaluator::evaluate_threats(const state& s, bool color)
{
    score_t score = 0;
    auto [present_t, present_c] = s.get_present();
    
    // Check if opponent is in check
    if (present_c == color) {
        auto checks = s.find_checks(!color);
        if (checks.first().has_value()) {
            score += 50; // Bonus for putting opponent in check
        }
    }
    
    // TODO: Evaluate hanging pieces, forks, pins, etc.
    
    return score;
}

score_t Evaluator::evaluate_tempo(const state& s, bool color)
{
    score_t score = 0;
    auto [present_t, present_c] = s.get_present();
    
    // Tempo advantage for having the move
    if (present_c == color) {
        score += 10;
    }
    
    // Development bonus in early game
    // In 5D chess, time travel makes this complex
    // We'll use a simple heuristic based on present_t
    if (present_t < 10) {
        // Early game: prefer development
        score += 5;
    }
    
    return score;
}

score_t Evaluator::evaluate(const state& s)
{
    auto [present_t, present_c] = s.get_present();
    bool color = present_c;
    
    // Check for terminal states
    auto submit_state = s.can_submit();
    if (!submit_state.has_value()) {
        // Need to make moves - check if any legal moves exist
        auto [hc_info, ss] = HC_info::build_HC(s);
        if (!hc_info.search(ss).first().has_value()) {
            // No legal moves - check for checkmate or stalemate
            auto phantom = s.phantom();
            if (phantom.find_checks(!color).first().has_value()) {
                // Checkmate!
                return -SCORE_MATE;
            } else {
                // Stalemate
                return SCORE_DRAW;
            }
        }
    }
    
    // Combine evaluation components with weights
    score_t score = 0;
    
    // Material is most important
    score += evaluate_material(s, color);
    
    // Timeline control (important in 5D chess)
    score += evaluate_timeline_control(s, color);
    
    // Mobility
    score += evaluate_mobility(s, color);
    
    // King safety
    score += evaluate_king_safety(s, color);
    
    // Threats
    score += evaluate_threats(s, color);
    
    // Tempo
    score += evaluate_tempo(s, color);
    
    return score;
}

//=============================================================================
// ActionSampler Implementation
//=============================================================================

score_t ActionSampler::score_capture(const state& s, const moveseq& action)
{
    score_t score = 0;
    auto [present_t, present_c] = s.get_present();
    
    for (const auto& m : action) {
        piece_t captured = s.get_piece(m.to, present_c);
        if (captured != NO_PIECE && captured != WALL_PIECE) {
            score += Evaluator::get_piece_value(captured) * 10;
            
            // MVV-LVA: Most Valuable Victim - Least Valuable Attacker
            piece_t attacker = s.get_piece(m.from, present_c);
            score += (1000 - Evaluator::get_piece_value(attacker));
        }
    }
    return score;
}

score_t ActionSampler::score_check(const state& s, const moveseq& action)
{
    // Approximate: checking moves get bonus
    // Actually checking requires applying the move which is expensive
    return 0; // TODO: Implement when we have fast check detection
}

score_t ActionSampler::score_timeline_jump(const state& s, const moveseq& action)
{
    score_t score = 0;
    
    for (const auto& m : action) {
        // Moves that create new timelines or jump between them
        if (m.from.l() != m.to.l()) {
            score += 500; // Bonus for timeline-affecting moves
            
            // Extra bonus for jumping into active timelines
            auto [active_min, active_max] = s.get_active_range();
            if (m.to.l() >= active_min && m.to.l() <= active_max) {
                score += 200;
            }
        }
        
        // Time travel moves
        if (m.from.t() != m.to.t()) {
            score += 300;
        }
    }
    
    return score;
}

score_t ActionSampler::score_center_control(const state& s, const moveseq& action)
{
    score_t score = 0;
    auto [size_x, size_y] = s.get_board_size();
    int center_x = size_x / 2;
    int center_y = size_y / 2;
    
    for (const auto& m : action) {
        // Distance from center (lower is better)
        int dx = std::abs(m.to.x() - center_x);
        int dy = std::abs(m.to.y() - center_y);
        int dist = dx + dy;
        
        // Bonus for moving towards center
        score += (8 - dist) * 5;
    }
    
    return score;
}

score_t ActionSampler::score_action(const state& s, const moveseq& action)
{
    score_t score = 0;
    
    score += score_capture(s, action);
    score += score_check(s, action);
    score += score_timeline_jump(s, action);
    score += score_center_control(s, action);
    
    return score;
}

std::vector<std::pair<moveseq, score_t>> ActionSampler::sample_actions(
    const state& s,
    const HC_info& hc_info,
    search_space& ss,
    int max_actions,
    std::mt19937& rng)
{
    std::vector<std::pair<moveseq, score_t>> actions;
    actions.reserve(max_actions);
    
    // Enumerate actions (up to limit)
    int count = 0;
    for (moveseq mvs : hc_info.search(ss)) {
        score_t priority = score_action(s, mvs);
        actions.emplace_back(std::move(mvs), priority);
        
        if (++count >= max_actions * 2) {
            // Collected enough for sampling
            break;
        }
    }
    
    // Sort by priority (descending)
    std::sort(actions.begin(), actions.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Keep top actions
    if (actions.size() > static_cast<size_t>(max_actions)) {
        actions.resize(max_actions);
    }
    
    return actions;
}

//=============================================================================
// Bot Implementation
//=============================================================================

Bot::Bot(BotConfig config)
    : config(std::move(config))
    , tt(this->config.tt_size_mb)
    , should_stop(false)
{
}

Bot::~Bot()
{
    // Ensure any running search is stopped
    stop();
}

bool Bot::time_up() const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start);
    return elapsed.count() >= config.time_limit_ms;
}

bool Bot::should_stop_search() const
{
    return should_stop.load() || 
           stats.nodes_searched >= config.max_nodes ||
           time_up();
}

uint64_t Bot::compute_hash(const state& s) const
{
    return ZobristHash::instance().hash(s);
}

score_t Bot::negamax(state& s, int depth, score_t alpha, score_t beta, moveseq& pv, int thread_id, std::mt19937& rng)
{
    // Check termination conditions
    if (should_stop_search()) {
        return 0;
    }
    
    stats.nodes_searched.fetch_add(1, std::memory_order_relaxed);
    
    // Check transposition table
    uint64_t hash = compute_hash(s);
    if (config.use_transposition_table) {
        TTEntry tt_entry;
        if (tt.probe(hash, tt_entry)) {
            stats.tt_hits.fetch_add(1, std::memory_order_relaxed);
            int16_t tt_depth = tt_entry.depth.load(std::memory_order_relaxed);
            if (tt_depth >= depth) {
                uint8_t tt_flag = tt_entry.flag.load(std::memory_order_relaxed);
                score_t tt_score = tt_entry.score.load(std::memory_order_relaxed);
                
                if (tt_flag == TTEntry::FLAG_EXACT) {
                    pv = tt_entry.best_move;
                    stats.tt_cutoffs.fetch_add(1, std::memory_order_relaxed);
                    return tt_score;
                } else if (tt_flag == TTEntry::FLAG_LOWER) {
                    alpha = std::max(alpha, tt_score);
                } else if (tt_flag == TTEntry::FLAG_UPPER) {
                    beta = std::min(beta, tt_score);
                }
                if (alpha >= beta) {
                    stats.tt_cutoffs.fetch_add(1, std::memory_order_relaxed);
                    return tt_score;
                }
            }
        }
    }
    
    // Terminal depth
    if (depth <= 0) {
        return Evaluator::evaluate(s);
    }
    
    // Check if we can submit (turn ends without moves)
    auto submit_state = s.can_submit();
    if (submit_state.has_value()) {
        // Recurse with submitted state
        moveseq child_pv;
        score_t score = -negamax(*submit_state, depth - 1, -beta, -alpha, child_pv, thread_id, rng);
        return score;
    }
    
    // Generate legal actions
    auto [hc_info, ss] = HC_info::build_HC(s);
    
    // Sample or enumerate actions
    std::vector<std::pair<moveseq, score_t>> actions;
    if (config.use_action_sampling) {
        actions = ActionSampler::sample_actions(s, hc_info, ss, config.max_actions_per_ply, rng);
    } else {
        // Full enumeration
        for (moveseq mvs : hc_info.search(ss)) {
            score_t priority = config.use_move_ordering ? ActionSampler::score_action(s, mvs) : 0;
            actions.emplace_back(std::move(mvs), priority);
        }
        if (config.use_move_ordering) {
            std::sort(actions.begin(), actions.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
        }
    }
    
    if (actions.empty()) {
        // No legal moves - checkmate or stalemate
        auto [t, c] = s.get_present();
        auto phantom = s.phantom();
        if (phantom.find_checks(!c).first().has_value()) {
            return -SCORE_MATE + (config.max_depth - depth); // Prefer faster mates
        } else {
            return SCORE_DRAW;
        }
    }
    
    // Search moves
    score_t best_score = -SCORE_INFINITY;
    moveseq best_move;
    uint8_t tt_flag = TTEntry::FLAG_UPPER;
    
    int move_count = 0;
    for (const auto& [mvs, priority] : actions) {
        if (should_stop_search()) break;
        
        // Apply action
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs),
                      [](full_move m) { return ext_move(m); });
        action act = action::from_vector(emvs, s);
        auto new_state_opt = s.can_apply(act);
        
        if (!new_state_opt.has_value()) continue;
        
        state& new_state = *new_state_opt;
        
        // Late Move Reduction
        int reduction = 0;
        if (config.use_late_move_reduction && move_count > 3 && depth > 2) {
            reduction = 1;
        }
        
        moveseq child_pv;
        score_t score;
        
        if (reduction > 0) {
            // Reduced search
            score = -negamax(new_state, depth - 1 - reduction, -beta, -alpha, child_pv, thread_id, rng);
            if (score > alpha) {
                // Re-search at full depth
                score = -negamax(new_state, depth - 1, -beta, -alpha, child_pv, thread_id, rng);
            }
        } else {
            score = -negamax(new_state, depth - 1, -beta, -alpha, child_pv, thread_id, rng);
        }
        
        if (score > best_score) {
            best_score = score;
            best_move = mvs;
            pv = mvs;
        }
        
        if (score > alpha) {
            alpha = score;
            tt_flag = TTEntry::FLAG_EXACT;
        }
        
        if (alpha >= beta) {
            tt_flag = TTEntry::FLAG_LOWER;
            break; // Beta cutoff
        }
        
        move_count++;
    }
    
    // Store in transposition table
    if (config.use_transposition_table && !should_stop_search()) {
        tt.store(hash, best_score, depth, tt_flag, best_move);
    }
    
    return best_score;
}

std::optional<action> Bot::find_best_action(const state& s)
{
    return search_with_callback(s, nullptr);
}

// Search root position at a specific depth
void Bot::search_root(const state& s, int depth, int thread_id, std::mt19937& rng)
{
    // Generate all legal actions
    auto [hc_info, ss] = HC_info::build_HC(s);
    
    std::vector<std::pair<moveseq, score_t>> actions;
    if (config.use_action_sampling) {
        actions = ActionSampler::sample_actions(s, hc_info, ss, config.max_actions_per_ply, rng);
    } else {
        for (moveseq mvs : hc_info.search(ss)) {
            score_t priority = config.use_move_ordering ? ActionSampler::score_action(s, mvs) : 0;
            actions.emplace_back(std::move(mvs), priority);
        }
        if (config.use_move_ordering) {
            std::sort(actions.begin(), actions.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
        }
    }
    
    // Shuffle slightly for different threads to search different orders
    // This helps Lazy SMP find different lines
    if (thread_id > 0 && actions.size() > 1) {
        // Partial shuffle: swap some elements
        for (size_t i = 0; i < std::min(actions.size() / 4, size_t(5)); ++i) {
            size_t j = rng() % actions.size();
            size_t k = rng() % actions.size();
            if (j != k) std::swap(actions[j], actions[k]);
        }
    }
    
    score_t alpha = -SCORE_INFINITY;
    score_t beta = SCORE_INFINITY;
    score_t best_score = -SCORE_INFINITY;
    moveseq best_move;
    
    for (const auto& [mvs, priority] : actions) {
        if (should_stop_search()) break;
        
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs),
                      [](full_move m) { return ext_move(m); });
        action act = action::from_vector(emvs, s);
        auto new_state_opt = s.can_apply(act);
        
        if (!new_state_opt.has_value()) continue;
        
        moveseq child_pv;
        score_t score = -negamax(*new_state_opt, depth - 1, -beta, -alpha, child_pv, thread_id, rng);
        
        if (score > best_score) {
            best_score = score;
            best_move = mvs;
            
            // Update global best if better
            score_t current_global = global_best_score.load(std::memory_order_relaxed);
            if (score > current_global) {
                std::lock_guard<std::mutex> lock(best_move_mutex);
                // Double-check after acquiring lock
                if (score > global_best_score.load(std::memory_order_relaxed)) {
                    global_best_score.store(score, std::memory_order_relaxed);
                    global_best_move = mvs;
                }
            }
        }
        
        alpha = std::max(alpha, score);
    }
}

// Worker thread for parallel search
void Bot::search_worker(const state& s, int target_depth, int thread_id)
{
    std::mt19937 rng(std::random_device{}() + thread_id * 12345);
    
    // Lazy SMP: different threads search with slightly different depths
    // Thread 0 is main thread, others help with varied depths
    int depth_offset = 0;
    if (thread_id > 0) {
        // Alternate between same depth and one less
        depth_offset = (thread_id % 2 == 0) ? 0 : -1;
        if (target_depth + depth_offset < 1) depth_offset = 0;
    }
    
    int search_depth = target_depth + depth_offset;
    
    // Search until stopped
    while (!should_stop_search() && search_depth <= config.max_depth) {
        search_root(s, search_depth, thread_id, rng);
        
        // Non-main threads continue to deeper depths
        if (thread_id > 0) {
            search_depth++;
        } else {
            break; // Main thread returns after one iteration
        }
    }
}

std::optional<action> Bot::search_with_callback(
    const state& s,
    std::function<void(int depth, score_t score, const moveseq& pv)> callback)
{
    stats.reset();
    should_stop.store(false, std::memory_order_release);
    search_start = std::chrono::steady_clock::now();
    global_best_score.store(-SCORE_INFINITY, std::memory_order_relaxed);
    global_best_move.clear();
    completed_depth.store(0, std::memory_order_relaxed);
    tt.new_search();
    
    // Check if we need to make moves
    auto submit_state = s.can_submit();
    if (submit_state.has_value()) {
        // Can submit without moves - this shouldn't happen in normal play
        return std::nullopt;
    }
    
    // Generate all legal actions first to check for trivial cases
    auto [hc_info, ss] = HC_info::build_HC(s);
    
    std::vector<moveseq> all_actions;
    for (moveseq mvs : hc_info.search(ss)) {
        all_actions.push_back(std::move(mvs));
        if (all_actions.size() > 1) break; // We only need to know if there's more than one
    }
    
    if (all_actions.empty()) {
        // No legal moves
        return std::nullopt;
    }
    
    if (all_actions.size() == 1) {
        // Only one legal move - return immediately
        std::vector<ext_move> emvs;
        std::transform(all_actions[0].begin(), all_actions[0].end(), std::back_inserter(emvs),
                      [](full_move m) { return ext_move(m); });
        return action::from_vector(emvs, s);
    }
    
    // Determine number of threads
    unsigned int num_threads = config.num_threads;
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    
    // For very short searches, use single thread
    if (!config.use_parallel_search || config.max_depth <= 2 || num_threads == 1) {
        num_threads = 1;
    }
    
    // Iterative deepening
    moveseq best_move;
    score_t best_score = -SCORE_INFINITY;
    
    int start_depth = config.use_iterative_deepening ? 1 : config.max_depth;
    std::mt19937 main_rng(std::random_device{}());
    
    for (int depth = start_depth; depth <= config.max_depth; ++depth) {
        if (should_stop_search()) break;
        
        global_best_score.store(-SCORE_INFINITY, std::memory_order_relaxed);
        
        if (num_threads > 1) {
            // Parallel search using Lazy SMP
            std::vector<std::thread> workers;
            workers.reserve(num_threads - 1);
            
            // Start helper threads
            for (unsigned int i = 1; i < num_threads; ++i) {
                workers.emplace_back(&Bot::search_worker, this, std::ref(s), depth, i);
            }
            
            // Main thread searches too
            search_root(s, depth, 0, main_rng);
            
            // Wait for all workers to complete this depth or be stopped
            for (auto& t : workers) {
                t.join();
            }
        } else {
            // Single-threaded search
            moveseq pv;
            state s_copy = s;
            score_t score = negamax(s_copy, depth, -SCORE_INFINITY, SCORE_INFINITY, pv, 0, main_rng);
            
            if (!should_stop_search() && !pv.empty()) {
                std::lock_guard<std::mutex> lock(best_move_mutex);
                global_best_move = pv;
                global_best_score.store(score, std::memory_order_relaxed);
            }
        }
        
        // Check results from this depth
        {
            std::lock_guard<std::mutex> lock(best_move_mutex);
            if (!global_best_move.empty()) {
                best_move = global_best_move;
                best_score = global_best_score.load(std::memory_order_relaxed);
                stats.depth_reached = depth;
                stats.best_score = best_score;
                
                if (callback) {
                    callback(depth, best_score, best_move);
                }
                
                if (config.verbose) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start);
                    std::cout << "depth " << depth 
                              << " score " << best_score 
                              << " nodes " << stats.nodes_searched.load(std::memory_order_relaxed)
                              << " time " << elapsed.count() << "ms"
                              << " threads " << num_threads
                              << std::endl;
                }
            }
        }
        
        // Early exit if we found a mate
        if (std::abs(best_score) >= SCORE_MATE - 100) {
            break;
        }
    }
    
    // Record final time
    auto now = std::chrono::steady_clock::now();
    stats.time_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start).count();
    stats.search_complete = !should_stop_search();
    
    if (best_move.empty()) {
        // Fallback: return first legal action
        auto [hc_info2, ss2] = HC_info::build_HC(s);
        auto first_action = hc_info2.search(ss2).first();
        if (first_action) {
            best_move = *first_action;
        } else {
            return std::nullopt;
        }
    }
    
    // Convert moveseq to action
    std::vector<ext_move> emvs;
    std::transform(best_move.begin(), best_move.end(), std::back_inserter(emvs),
                  [](full_move m) { return ext_move(m); });
    return action::from_vector(emvs, s);
}

} // namespace bot
