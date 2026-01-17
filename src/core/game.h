// game.h
// interface for python library
#ifndef GAME_H
#define GAME_H

#include <vector>
#include <tuple>
#include <string>
#include <map>
#include <set>
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
    
    const state &get_current_state() const;
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

    comments_t get_comments() const;
    void set_comments(const comments_t &c) const;
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
