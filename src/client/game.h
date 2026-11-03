// game.h
// interface for python library
#ifndef GAME_H
#define GAME_H

#include <vector>
#include <tuple>
#include <string>
#include <map>
#include <set>
#include <optional>
#include <ostream>
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
    std::optional<state> preview;
    pgnparser_ast::header_map headers;
    
    game(std::unique_ptr<gnode<comments_t>> gt);
    mutable std::optional<bool> nonterminal;
    void fresh();
public:
    enum class match_status_t {PLAYING, WHITE_WINS, BLACK_WINS, STALEMATE, ILLEGAL_HISTORY};

    struct load_options {
        bool allow_illegal_history = false;
        // With allow_illegal_history, setup legality is unchecked for display preparation.
        bool initial_position_only = false;
    };

    std::optional<std::string> get_header(const std::string &key) const;
    // Size, Timeline, and Promotions are derived from the initial position.
    void set_header(const std::string &key, const std::string &value);
    std::vector<std::string> header_keys() const;

    static game from_pgn(std::string str, load_options options = {false, false});

    // Flags describe submitted history, not temporary draft moves or previews.
    bool has_illegal_history() const noexcept;
    
    const state &get_current_state() const;
    const state &get_unmoved_state() const;
    std::pair<int, bool> get_current_present() const;
    std::vector<ext_move> get_cached_moves() const;
    std::vector<boards_info_t> get_current_boards() const;
    std::pair<std::vector<boards_info_t>, std::vector<full_move>> get_phantom_boards_and_checks() const;
    std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> get_current_timeline_status() const;
    std::vector<vec4> gen_move_if_playable(vec4 p) const;
    
    match_status_t get_match_status() const;
    std::vector<vec4> get_movable_pieces() const;
    
    bool is_playable(vec4 p) const;
    bool can_undo() const;
    bool can_redo() const;
    bool can_submit(bool allow_submit_with_checks = false) const;
    bool undo();
    bool redo();
    bool apply_move(ext_move m);
    bool preview_move(ext_move m);
    void discard_preview();
    bool submit(bool allow_submit_with_checks = false);
    // Extract a completed draft, including drafts with royal exposure.
    std::optional<action> get_submit_action() const;
    bool currently_check() const;
    std::vector<std::pair<vec4,vec4>> get_current_checks() const;
    std::pair<int, int> get_board_size() const;
    promotion_options get_promotion_options() const;
    
    std::optional<action> suggest_action();
    // Insert a validated continuation without navigating or clearing draft moves.
    bool add_child(action act);
    bool has_child(const action &act) const;
    // Remove a direct continuation without changing the current draft or history.
    bool delete_child(const action &act);

    comments_t get_comments() const;
    void set_comments(const comments_t &c) const;
    bool has_parent() const;
    void visit_parent();
    struct action_summary {
        action act;
        std::string pgn;
        // Includes illegal history inherited from earlier actions or the setup.
        bool is_illegal;
    };
    std::vector<action_summary> get_child_actions(
        pgn_options show_flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION) const;
    std::vector<std::tuple<action, std::string>> get_historical_actions() const;
    // Navigate only to an existing child; return false without mutation if absent.
    bool visit_child(const action &act);
    
    std::string show_pgn(
        pgn_options show_flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION | pgn_options::SHOW_MATE,
        bool complete_game_tree = true,
        bool standard_metadata_only = false
    );
};


[[maybe_unused]]
static std::ostream &operator<<(std::ostream &os, const game::match_status_t &status)
{
   switch (status)
   {
       case game::match_status_t::PLAYING:
           os << "PLAYING";
           break;
       case game::match_status_t::WHITE_WINS:
           os << "WHITE_WINS";
           break;
       case game::match_status_t::BLACK_WINS:
           os << "BLACK_WINS";
           break;
       case game::match_status_t::STALEMATE:
           os << "STALEMATE";
           break;
       case game::match_status_t::ILLEGAL_HISTORY:
           os << "ILLEGAL_HISTORY";
           break;
   }
   return os;
}


#endif // GAME_H
