//
//  pgnparser.h
//  parser
//
//  Created by ftxi on 2025/10/26.
//

#ifndef pgnparser_h
#define pgnparser_h

#include "pgnerror.h"
#include "ast.h"
#include <cstdint>
#include <stdexcept>

class pgnparser
{
private:
    const bool check_turn_number;
    std::shared_ptr<const std::string> source;
    std::string_view input;
    struct {
        std::string_view::const_iterator current;
        std::size_t start, end, previous_end;
        pgnparser_ast::token_t token;
        char piece;
        char file;
        unsigned number;
        ::turn turn;
        // A requested first turn of 0w has no representable predecessor.
        bool before_turn_zero = false;
        std::string_view comment;
    } buffer{};
    std::optional<parse_error> furthest_error;

    [[noreturn]] void fail(parse_error::error_type_t reason) const;
    void record_expected(std::vector<std::string> expected);
public:
    
    pgnparser(std::string msg, bool check_turn_number=true, turn start_turn={1,false});
    void next_token();
    void test_lexer();
     
    using relative_board = pgnparser_ast::relative_board;
    using absolute_board = pgnparser_ast::absolute_board;
    using physical_move = pgnparser_ast::physical_move;
    using superphysical_move = pgnparser_ast::superphysical_move;
    using move = pgnparser_ast::move;
    using actions = pgnparser_ast::actions;
    using gametree = pgnparser_ast::gametree;
    using game = pgnparser_ast::game;
    
    std::optional<relative_board> parse_relative_board();
    std::optional<absolute_board> parse_absolute_board();
    std::optional<physical_move> parse_physical_move();
    std::optional<std::monostate> parse_timeline_comment();
    std::optional<superphysical_move> parse_superphysical_move();
    std::optional<move> parse_move(); /* parses move in a larger context */
    std::optional<move> parse_standalone_move(); /* parses move followed by END */
private:
    std::optional<char> parse_promotion();
    // made private because the return value relies on the lifetime of this->input
    std::vector<std::string_view> parse_comments();
public:
    std::optional<actions> parse_actions();
    std::optional<gametree> parse_gametree();
    // Setup-only parsing leaves the move history unparsed; lexer lookahead remains.
    std::optional<game> parse_game(bool initial_position_only = false);

    // Parse a metadata string that represents a board FEN entry
    // Expected format: <fen-string> ':' ['+' | '-' | NIL] <line> ':' <time> ':' {'w'|'b'}
    // Throws parse_error on malformed input.
    static std::tuple<std::string, pgnparser_ast::token_t, int, int, bool> parse_board_fen_metadata(const std::string &s);
    
    static bool match_absolute_board(absolute_board simple, absolute_board full);
    static bool match_relative_board(relative_board simple, relative_board full);
    static bool match_physical_move(physical_move a, physical_move b);
    static bool match_superphysical_move(superphysical_move a, superphysical_move b);
    static bool match_move(move a, move b);
};

#endif /* pgnparser_h */
